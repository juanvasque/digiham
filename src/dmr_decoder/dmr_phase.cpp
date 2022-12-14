#include <cstring>
#include "dmr_phase.hpp"
#include "dmr_meta.hpp"
#include "emb.hpp"
#include "cach.hpp"
#include "slottype.hpp"
#include "gps.hpp"

extern "C" {
#include "hamming_distance.h"
#include "bptc_196_96.h"
}

#include <iostream>

using namespace Digiham::Dmr;

int Phase::getSyncType(unsigned char *potentialSync) {
    if (hamming_distance((uint8_t*) potentialSync, (uint8_t*) dmr_bs_data_sync, SYNC_SIZE) <= 3) {
        return SYNCTYPE_DATA;
    }
    if (hamming_distance((uint8_t*) potentialSync, (uint8_t*) dmr_bs_voice_sync, SYNC_SIZE) <= 3) {
        return SYNCTYPE_VOICE;
    }
    if (hamming_distance((uint8_t*) potentialSync, (uint8_t*) dmr_ms_data_sync, SYNC_SIZE) <= 3) {
        return SYNCTYPE_DATA;
    }
    if (hamming_distance((uint8_t*) potentialSync, (uint8_t*) dmr_ms_voice_sync, SYNC_SIZE) <= 3) {
        return SYNCTYPE_VOICE;
    }

    return -1;
}

int SyncPhase::getRequiredData() {
    return SYNC_SIZE + syncOffset;
};

Digiham::Phase* SyncPhase::process(Csdr::Reader<unsigned char>* data, Csdr::Writer<unsigned char>* output) {
    int syncType = getSyncType(data->getReadPointer() + syncOffset);
    if (syncType > 0) return new FramePhase();

    // as long as we don't find any sync, move ahead, symbol by symbol
    data->advance(1);
    // tell decoder that we'll continue
    return this;
}

FramePhase::FramePhase():
    embCollectors { new EmbeddedCollector(), new EmbeddedCollector() },
    talkerAliasCollector { new TalkerAliasCollector(), new TalkerAliasCollector }
{}

FramePhase::~FramePhase() {
    delete embCollectors[0];
    delete embCollectors[1];
    delete talkerAliasCollector[0];
    delete talkerAliasCollector[1];
}

int FramePhase::getRequiredData() {
    return FRAME_SIZE;
}

Digiham::Phase *FramePhase::process(Csdr::Reader<unsigned char> *data, Csdr::Writer<unsigned char> *output) {
    Cach* cach = Cach::parse(data->getReadPointer());
    // slots should always be alternating, but may be overridden by 100% correct tact
    // this is our assumption of what the next slot should be, based on the last slot:
    unsigned char next = slot ^ 1;
    if (cach->hasTact()) {
        // is our assumption correct?
        if (cach->getTact()->getSlot() != next) {
            // no. act according to the level of confidence aka. slotStability
            if (slotStability < 5) {
                slotStability = 0;
                slot = cach->getTact()->getSlot();
                // if we do switch, reset the other slot
                unsigned char otherSlot = slot ^ 1;
                syncTypes[otherSlot] = -1;
                ((MetaCollector*) meta)->withSlot(otherSlot, [] (Slot* s) { s->reset(); });
                if (activeSlot == otherSlot) activeSlot = -1;
            } else {
                // no need to check for a lower boundary since once the stability goes below the threshold, the other
                // branch of this if kicks in
                slotStability--;
                if (slot != -1) {
                    slot = next;
                }
            }
        } else {
            if (++slotStability > 100) slotStability = 100;
            slot = next;
        }
    } else if (slot != -1) {
        if (slotStability-- < -100) slotStability = -100;
        slot = next;
    }

    delete cach;

    if (slot != -1) {
        int syncType = getSyncType(data->getReadPointer() + syncOffset);
        if (syncType > 0) {
            // increase sync count, cap at 5
            if (++syncCount > 5) syncCount = 5;
            if (++slotSyncCount[slot] > 5) slotSyncCount[slot] = 5;

            // trigger a softreset only when we are dropping out of voice
            bool softReset = syncTypes[slot] == SYNCTYPE_VOICE && syncType != syncTypes[slot];
            syncTypes[slot] = syncType;
            ((MetaCollector*) meta)->withSlot(slot, [syncType, softReset] (Slot* s) {
                s->setSync(syncType);
                if (softReset) s->softReset();
            });
            superframeCounter[slot] = 0;
            embCollectors[slot]->reset();
        } else if (syncTypes[slot] == SYNCTYPE_VOICE && superframeCounter[slot] < 5) {
            // voice mode has superframes consisting of 6 normal frames
            // the first frame is expected to have a normal sync (covered above)
            // the remaining five contain embedded data
            superframeCounter[slot]++;

            uint16_t emb_data = 0;

            // try to decode as embedded signalling
            for (int i = 0; i < 2; i++) {
                unsigned int offset = syncOffset + i * 20;
                unsigned char* raw = data->getReadPointer() + offset;
                for (int k = 0; k < 4; k++) {
                    emb_data = (emb_data << 2) | raw[k];
                }
            }

            Emb* emb = Emb::parse(emb_data);
            if (emb != nullptr) {
                // if the EMB decoded correctly, that counts towards the sync :)
                if (++syncCount > 5) syncCount = 5;
                if (++slotSyncCount[slot] > 5) slotSyncCount[slot] = 5;

                // parse actual embedded data
                unsigned char embedded_data[4] = {0};
                unsigned char* emb_raw = data->getReadPointer() + syncOffset + 4;
                for (int i = 0; i < 16; i++) {
                    embedded_data[i / 4] |= emb_raw[i] << (6 - (i % 4) * 2);
                }
                auto collector = embCollectors[slot];
                switch (emb->getLcss()) {
                    case LCSS_SINGLE:
                        // RC data. no idea what to do with that yet.
                        break;

                    case LCSS_START:
                        collector->reset();
                        // break intentionally omitted

                    case LCSS_CONTINUATION:
                        collector->collect(embedded_data);
                        break;

                    case LCSS_STOP: {
                        collector->collect(embedded_data);
                        Lc* lc = collector->getLc();
                        if (lc != nullptr) {
                            handleLc(lc);
                            delete lc;
                        }
                        collector->reset();
                        break;
                    }
                }

                delete emb;
            } else {
                // no sync and no EMB, decrease sync counter
                if (--slotSyncCount[slot] < 0) {
                    slotSyncCount[slot] = 0;
                    syncTypes[slot] = -1;
                    ((MetaCollector*) meta)->withSlot(slot, [] (Slot* s) {
                        s->reset();
                    });
                    if (activeSlot == slot) activeSlot = -1;
                }
                if (--syncCount < 0) {
                    ((MetaCollector*) meta)->reset();
                    return new SyncPhase();
                }
            }
        } else {
            // even if we didn't find the sync, we should still reset the superframe counter
            superframeCounter[slot] = 0;
            embCollectors[slot]->reset();
            // sync expected, but not found, decrease sync counter
            if (--slotSyncCount[slot] < 0) {
                slotSyncCount[slot] = 0;
                syncTypes[slot] = -1;
                ((MetaCollector*) meta)->withSlot(slot, [] (Slot* s) {
                    s->reset();
                });
                if (activeSlot == slot) activeSlot = -1;
            }
            if (--syncCount < 0) {
                ((MetaCollector*) meta)->reset();
                return new SyncPhase();
            }
        }

        if (syncTypes[slot] == SYNCTYPE_VOICE) {
            // don't output anything if the slot is muted
            // don't output anything if the other slot is active
            if (((slot + 1) & slotFilter) && (activeSlot == -1 || activeSlot == slot)) {
                activeSlot = slot;
                // extract payload data
                unsigned char* payload = output->getWritePointer();
                std::memset(payload, 0, 27);
                // first half
                unsigned char* payloadRaw = data->getReadPointer() + CACH_SIZE;
                for (int i = 0; i < 54; i++) {
                    payload[i / 4] |= (payloadRaw[i] & 3) << (6 - 2 * (i % 4));
                }

                // second half
                payloadRaw += 54 + SYNC_SIZE;
                for (int i = 0; i < 54; i++) {
                    payload[(i + 54) / 4] |= (payloadRaw[i] & 3) << (6 - 2 * ((i + 54) % 4));
                }
                output->advance(27);
            }
        } else {
            if (activeSlot == slot) {
                activeSlot = -1;
            }

            talkerAliasCollector[slot]->reset();

            if (syncTypes[slot] == SYNCTYPE_DATA) {
                uint32_t slot_type = 0;
                unsigned char* slot_type_raw = data->getReadPointer() + syncOffset - 5;
                for (int i = 0; i < 5; i++) {
                    slot_type = (slot_type << 2) | (slot_type_raw[i] & 3);
                }

                slot_type_raw = data->getReadPointer() + syncOffset + SYNC_SIZE;
                for (int i = 0; i < 5; i++) {
                    slot_type = (slot_type << 2) | (slot_type_raw[i] & 3);
                }

                SlotType* slotType = SlotType::parse(slot_type);
                if (slotType != nullptr) {
                    uint8_t data_type = slotType->getDataType();

                    // according to ETSI 6.2, rate 3/4 data is the only kind of data that doesn't use the BPTC(196,96) FEC
                    if (data_type == DATA_TYPE_RATE_3_4_DATA) {
                        // not decoded
                    } else {
                        // extract payload data
                        uint8_t payload[25] = { 0 };
                        // first half
                        unsigned char* payloadRaw = data->getReadPointer() + CACH_SIZE;
                        // 54 - 5 = 49 // compensate for the slot type PDU
                        for (int k = 0; k < 49; k++) {
                            payload[k / 4] |= (payloadRaw[k] & 3) << (6 - 2 * (k % 4));
                        }

                        // second half
                        // +5 again for the slot type PDU
                        payloadRaw += 54 + SYNC_SIZE + 5;
                        for (int k = 0; k < 49; k++) {
                            payload[(k + 49) / 4] |= (payloadRaw[k] & 3) << (6 - 2 * ((k + 49) % 4));
                        }

                        uint8_t lc_data[12] = { 0 };
                        if (bptc_196_96(payload, lc_data)) {
                            if (data_type == DATA_TYPE_VOICE_LC) {
                                Lc* lc = Lc::parseFromVoiceHeader(lc_data);
                                if (lc != nullptr) {
                                    handleLc(lc);
                                    delete lc;
                                }
                            } else if (data_type == DATA_TYPE_TERMINATOR_LC || data_type == DATA_TYPE_IDLE) {
                                ((MetaCollector*) meta)->withSlot(slot, [] (Slot* s) {
                                    s->softReset();
                                });
                            //} else if (data_type == DATA_TYPE_DATA_HEADER || data_type == DATA_TYPE_RATE_1_2_DATA) {
                                // not implemented
                            //} else {
                                // unsupported
                            }
                        }

                    }

                    delete slotType;
                }
            } else {
                ((MetaCollector*) meta)->withSlot(slot, [] (Slot* s) { s->reset(); });
            }
        }
    }

    data->advance(FRAME_SIZE);
    return this;
}

void FramePhase::handleLc(Lc *lc) {
    unsigned char opcode = lc->getOpCode();
    switch (opcode) {
        case LC_OPCODE_GROUP:
        case LC_OPCODE_UNIT_TO_UNIT:
            ((MetaCollector*) meta)->withSlot(slot, [lc] (Slot* s) { s->setFromLc(lc); });
            break;
        case LC_TALKER_ALIAS_HDR:
        case LC_TALKER_ALIAS_BLK1:
        case LC_TALKER_ALIAS_BLK2:
        case LC_TALKER_ALIAS_BLK3:
            // the actual opcodes are numbered consecutively, so this math makes sense
            talkerAliasCollector[slot]->setBlock(opcode - LC_TALKER_ALIAS_HDR, lc->getData());
            if (talkerAliasCollector[slot]->isComplete()) {
                std::string alias = talkerAliasCollector[slot]->getContents();
                // trim off any 0-chars (YSF bridging does this)
                auto end = alias.find_last_not_of('\0');
                alias = end == std::string::npos ? "" : alias.substr(0, end + 1);

                ((MetaCollector*) meta)->withSlot(slot, [alias] (Slot* s) {
                    s->setTalkerAlias(alias);
                });
            }
            break;
        case LC_GPS_INFO: {
            Coordinate* coord = Gps::parse(lc->getData());
            ((MetaCollector*) meta)->withSlot(slot, [coord] (Slot* s) {
                s->setCoordinate(coord);
            });
            break;
        }
        default:
            std::cerr << "unknown opcode: " << +opcode << " from feature set id: " << +lc->getFeatureSetId() << "\n";
            break;
    }
}

void FramePhase::setSlotFilter(unsigned char filter) {
    slotFilter = filter;
    // if the new filter mutes the active slot -> reset active slot
    if (((activeSlot + 1) & slotFilter) == 0) activeSlot = -1;
}