#pragma once

#include "phase.hpp"
#include "header.hpp"
#include "scrambler.hpp"
#include "dstar_meta.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <csdr/reader.hpp>

#define SYNC_SIZE 24
#define TERMINATOR_SIZE 48

namespace Digiham::DStar {

    class Phase: public Digiham::Phase {
        protected:
            const uint8_t header_sync[SYNC_SIZE] = {
                // part of the bitsync
                // the repeated 10s should always come ahead of the actual sync sequence, so we can use that to get
                // a more accurate sync and have the same length as the voice sync
                0, 1, 0, 1, 0, 1, 0, 1, 0,
                // actual sync
                1, 1, 1, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0
            };
            const uint8_t voice_sync[SYNC_SIZE] = {
                // 10 bits of 10
                1, 0, 1, 0, 1, 0, 1, 0, 1, 0,
                // two times 1101000
                1, 1, 0, 1, 0, 0, 0,
                1, 1, 0, 1, 0, 0, 0,
            };
            const uint8_t terminator[TERMINATOR_SIZE] = {
                // 32 bits of 10
                1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0,
                // 000100110101111 + 0,
                0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 0, 1, 1, 1, 1, 0,
            };
    };

    class SyncPhase: public Phase {
        public:
            int getRequiredData() override { return SYNC_SIZE; }
            Digiham::Phase* process(Csdr::Reader<unsigned char>* data, Csdr::Writer<unsigned char>* output) override;
    };

    class HeaderPhase: public Phase {
        public:
            int getRequiredData() override { return Header::bits; }
            Digiham::Phase* process(Csdr::Reader<unsigned char>* data, Csdr::Writer<unsigned char>* output) override;
    };

    class VoicePhase: public Phase {
        public:
            VoicePhase();
            explicit VoicePhase(int frameCount);
            ~VoicePhase() override;
            int getRequiredData() override { return 72 + 24 + 24; }
            Digiham::Phase* process(Csdr::Reader<unsigned char>* data, Csdr::Writer<unsigned char>* output) override;
        private:
            bool isSyncDue();
            void resetFrames();
            void collectDataFrame(unsigned char* data);
            void parseFrameData();
            int frameCount;
            int syncCount;
            void parseNMEAData(const std::string& data);
            Scrambler* scrambler;
            unsigned char collected_data[6] = { 0 };
            unsigned char message[20] = { 0 };
            unsigned char messageBlocks = 0;
            unsigned char header[41] = { 0 };
            unsigned char headerCount = 0;
            std::string simpleData;
    };

}