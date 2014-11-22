#pragma once
#include <BWTA/Region.h>

class MapAnalysisManager
{
    MapAnalysisManager();
    ~MapAnalysisManager() { };

public:
    BWTA::Region* home;
    BWTA::Chokepoint* choke;
    void init();
    void analyzeChoke();

    static MapAnalysisManager &MapAnalysisManager::Instance()
    {
        static MapAnalysisManager instance;
        return instance;
    }
};