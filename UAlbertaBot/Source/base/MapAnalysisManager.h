#pragma once
#include <BWTA/Region.h>

class MapAnalysisManager
{
	void findChokeWithSmallestGap();
	std::string getFileNameForChokepoint();

	MapAnalysisManager()
	{
		tempDir = "C:\\temp\\";
	}

	~MapAnalysisManager() { };

	BWTA::Region* home;
	BWTA::Chokepoint* choke;

	std::string readDir;
	std::string writeDir;
	std::string tempDir;

	void analyzeChoke();
	void initClingoProgramSource();
	std::string runASPSolver();
	void readSolution(std::string outputFile);
public:
	void init();

	static MapAnalysisManager &MapAnalysisManager::Instance()
	{
		static MapAnalysisManager instance;
		return instance;
	}

	std::vector<std::pair<BWAPI::UnitType, BWAPI::TilePosition> > &getWallInPositions();
};