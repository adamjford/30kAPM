#pragma once
class MapAnalysisManager
{
    MapAnalysisManager();
    ~MapAnalysisManager() { };

public:
    void init();

    static MapAnalysisManager &MapAnalysisManager::Instance()
    {
        static MapAnalysisManager instance;
        return instance;
    }
};