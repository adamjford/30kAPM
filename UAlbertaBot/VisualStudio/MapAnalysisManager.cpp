#include "MapAnalysisManager.h"
#include <BWTA.h>


MapAnalysisManager::MapAnalysisManager()
{
}

void MapAnalysisManager::init()
{
    BWTA::readMap();
    BWTA::analyze();
}