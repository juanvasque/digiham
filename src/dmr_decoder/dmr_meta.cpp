#include <cassert>
#include "dmr_meta.hpp"
#include "dmr_phase.hpp"

using namespace Digiham::Dmr;

Slot::~Slot() {
    delete coordinate;
}

void Slot::setSync(int sync) {
    if (this->sync == sync) return;
    this->sync = sync;
    setDirty();
}

void Slot::setType(int type) {
    if (this->type == type) return;
    this->type = type;
    setDirty();
}

void Slot::setSource(uint32_t source) {
    if (this->source == source) return;
    this->source = source;
    setDirty();
}

void Slot::setTarget(uint32_t target) {
    if (this->target == target) return;
    this->target = target;
    setDirty();
}

void Slot::setFromLc(Lc *lc) {
    switch (lc->getOpCode()) {
        case LC_OPCODE_GROUP:
            setType(META_TYPE_GROUP);
            break;
        case LC_OPCODE_UNIT_TO_UNIT:
            setType(META_TYPE_DIRECT);
            break;
        default:
            break;
    }
    setTarget(lc->getTarget());
    setSource(lc->getSource());
}

void Slot::setTalkerAlias(std::string alias) {
    if (talkerAlias == alias) return;
    talkerAlias = alias;
    setDirty();
}

void Slot::setCoordinate(Digiham::Coordinate *coord) {
    if (coordinate == coord || (coordinate != nullptr && coord != nullptr && *coordinate == *coord)) {
        delete coord;
        return;
    }
    // prevent race conditions
    auto old = coordinate;
    coordinate = coord;
    delete old;
    setDirty();
}

bool Slot::isDirty() {
    return dirty;
}

void Slot::setDirty() {
    dirty = true;
}

void Slot::setClean() {
    dirty = false;
}

void Slot::softReset() {
    setType(-1);
    setSource(0);
    setTarget(0);
    setTalkerAlias("");
    setCoordinate(nullptr);
}

void Slot::reset() {
    softReset();
    setSync(-1);
}

std::map<std::string, std::string> Slot::collect() {
    std::map<std::string, std::string> result;
    if (sync > 0) {
        result["sync"] = getSyncName();
    }
    if (type > 0) {
        result["type"] = getTypeName();
    }
    if (source > 0) {
        result["source"] = std::to_string(source);
    }
    if (target > 0) {
        result["target"] = std::to_string(target);
    }
    if (!talkerAlias.empty()) {
        result["talkeralias"] = talkerAlias;
    }
    if (coordinate != nullptr) {
        result["lat"] = std::to_string(coordinate->lat);
        result["lon"] = std::to_string(coordinate->lon);
    }
    return result;
}

std::string Slot::getSyncName() const {
    switch (sync) {
        case SYNCTYPE_DATA: return "data";
        case SYNCTYPE_VOICE: return "voice";
        default: return "unknown";
    }
}

std::string Slot::getTypeName() const {
    switch (type) {
        case META_TYPE_DIRECT: return "direct";
        case META_TYPE_GROUP: return "group";
        default: return "unknown";
    }
}

MetaCollector::MetaCollector():
    Digiham::MetaCollector(),
    slots { new Slot(), new Slot() }
{}

std::string MetaCollector::getProtocol() {
    return "DMR";
}

MetaCollector::~MetaCollector() {
    for (int i = 0; i < 2; i++) {
        delete slots[i];
    }
}

void MetaCollector::withSlot(int slot, const std::function<void(Slot *)>& callback) {
    assert(slot < 2);
    callback(slots[slot]);
    sendMetaDataForSlot(slot);
}

void MetaCollector::sendMetaData() {
    for (int i = 0; i < 2; i++) {
        sendMetaDataForSlot(i);
    }
}

void MetaCollector::sendMetaDataForSlot(int slotIndex) {
    Slot* slot = slots[slotIndex];
    if (!slot->isDirty()) return;

    auto metadata = Digiham::MetaCollector::collect();
    metadata["slot"] = std::to_string(slotIndex);

    auto slotMetadata = slot->collect();
    metadata.insert(slotMetadata.begin(), slotMetadata.end());

    Digiham::MetaCollector::sendMetaData(metadata);
    slot->setClean();
}

void MetaCollector::reset() {
    for (int i = 0; i < 2; i++) {
        slots[i]->reset();
    }
    sendMetaData();
}