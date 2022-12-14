#pragma once

#include "meta.hpp"
#include "gps.hpp"

namespace Digiham::Ysf {

    class MetaCollector: public Digiham::MetaCollector {
        public:
            ~MetaCollector() override;
            void reset();
            void setMode(std::string mode);
            void setDestination(std::string destination);
            void setSource(std::string source);
            void setUp(std::string up);
            void setDown(std::string down);
            void setRadio(std::string radio);
            void setGps(Coordinate* coordinate);
        protected:
            std::string getProtocol() override;
            std::map<std::string, std::string> collect() override;
            std::string mode;
            std::string destination;
            std::string source;
            std::string up;
            std::string down;
            std::string radio;
            Coordinate* coord = nullptr;
    };

}