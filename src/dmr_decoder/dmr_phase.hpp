#pragma once

#include "phase.hpp"

#define SYNC_SIZE 24

// full frame including CACH, SYNC and the two payload blocks
#define FRAME_SIZE 144

#define SYNCTYPE_DATA 1
#define SYNCTYPE_VOICE 2

namespace Digiham::Dmr {

    class Phase: public Digiham::Phase {
        protected:
            int getSyncType(unsigned char* potentialSync);

            const unsigned char dmr_bs_data_sync[SYNC_SIZE] =  { 3,1,3,3,3,3,1,1,1,3,3,1,1,3,1,1,3,1,3,3,1,1,3,1 };
            const unsigned char dmr_bs_voice_sync[SYNC_SIZE] = { 1,3,1,1,1,1,3,3,3,1,1,3,3,1,3,3,1,3,1,1,3,3,1,3 };
            const unsigned char dmr_ms_data_sync[SYNC_SIZE] =  { 3,1,1,1,3,1,1,3,3,3,1,3,1,3,3,3,3,1,1,3,1,1,1,3 };
            const unsigned char dmr_ms_voice_sync[SYNC_SIZE] = { 1,3,3,3,1,3,3,1,1,1,3,1,3,1,1,1,1,3,3,1,3,3,3,1 };

            // in DMR frames, the sync is in the middle. therefor, we need to be able to look at previous data once we
            // find a sync.
            // The data of one channel is 54 symbols, and the length of the CACH in basestation transmissions is 12.
            unsigned int syncOffset = 54 + 12;
    };

    class SyncPhase: public Phase {
        public:
            int getRequiredData() override;
            Digiham::Phase* process(Csdr::Reader<unsigned char>* data, Csdr::Writer<unsigned char>* output) override;
    };

    class FramePhase: public Phase {
        public:
            FramePhase();
            int getRequiredData() override;
            Digiham::Phase* process(Csdr::Reader<unsigned char>* data, Csdr::Writer<unsigned char>* output) override;
        private:
            int syncCount;
            int slot = -1;
            int slotStability = 0;
            int syncTypes[2] = {-1, -1};
            int activeSlot = -1;
            unsigned char slotFilter = 3;
    };

}