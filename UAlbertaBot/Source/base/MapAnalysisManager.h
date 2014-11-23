#pragma once
#include <BWTA/Region.h>

class MapAnalysisManager
{
    MapAnalysisManager();
    ~MapAnalysisManager() { };

    BWTA::Region* home;
    BWTA::Chokepoint* choke;

    void analyzeChoke();
    void runASPSolver();
public:
    void init();

    static MapAnalysisManager &MapAnalysisManager::Instance()
    {
        static MapAnalysisManager instance;
        return instance;
    }
};