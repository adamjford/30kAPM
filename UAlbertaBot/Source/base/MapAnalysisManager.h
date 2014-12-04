#pragma once
#include <BWTA/Region.h>

class MapAnalysisManager
{
    void findChokeWithSmallestGap();
    std::string getFileNameForChokepoint();

    ~MapAnalysisManager() { };

    BWTA::Region* home;
    BWTA::Chokepoint* choke;

    std::string readDir;
    std::string	writeDir;

    void analyzeChoke();
    void initClingoProgramSource();
    void runASPSolver();
    void readSolution();
public:
    void init();

    static MapAnalysisManager &MapAnalysisManager::Instance()
    {
        static MapAnalysisManager instance;
        return instance;
    }

    std::vector<std::pair<BWAPI::UnitType, BWAPI::TilePosition> > &getWallInPositions();
};