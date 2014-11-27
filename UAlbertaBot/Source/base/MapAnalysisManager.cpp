#include "Common.h"
#include <sys/stat.h>
#include "MapAnalysisManager.h"
#include <BWTA.h>
#include <cassert>
#include <fstream>
#include <sstream>

using namespace BWAPI;
using std::endl;

// constants
const int BTSize = 32; // build tile size
const int WTSize = 8; // walk tile size
// choke analysis
std::string clingoProgramText;
std::vector<std::pair<int, int>> buildTiles;
std::vector<std::pair<int, int>> supplyTiles;
std::vector<std::pair<int, int>> barracksTiles;
std::vector<std::pair<int, int>> walkableTiles;
std::vector<std::pair<int, int>> outsideTiles;
std::pair<int, int> insideBase, outsideBase;

bool optimizeGap = true;

std::pair<int, int> findClosestTile(const std::vector<std::pair<int, int>>& tiles);
std::pair<int, int> findFarthestTile(const std::vector<std::pair<int, int>>& tiles);
void initClingoProgramSource();

std::vector<std::pair<UnitType, TilePosition> > wallLayout;
bool wallData = true;
std::vector<std::pair<BWAPI::UnitType, BWAPI::TilePosition> > _wall;

MapAnalysisManager::MapAnalysisManager()
{
    // read in the name of the read and write directories from settings file
    struct stat buf;

    // if the file doesn't exist something is wrong so just set them to default settings
    if (stat(Options::FileIO::FILE_SETTINGS, &buf) != -1)
    {
        std::ifstream f_in(Options::FileIO::FILE_SETTINGS);
        getline(f_in, readDir);
        getline(f_in, writeDir);
        f_in.close();
    }
}

void MapAnalysisManager::findChokeWithSmallestGap()
{
    std::set<BWTA::Chokepoint*> chokepoints = home->getChokepoints();
    double min_length = 10000;

    // iterate through all chokepoints and look for the one with the smallest gap (least width)

    for (std::set<BWTA::Chokepoint*>::iterator c = chokepoints.begin(); c != chokepoints.end(); ++c)
    {
        double length = (*c)->getWidth();
        if (length < min_length || choke == NULL)
        {
            min_length = length;
            choke = *c;
        }
    }
}

void MapAnalysisManager::init()
{
    BWTA::readMap();
    BWTA::analyze();

    if (BWTA::getStartLocation(Broodwar->self()) != NULL)
    {
        home = BWTA::getStartLocation(Broodwar->self())->getRegion();
    }

    findChokeWithSmallestGap();

    analyzeChoke();
    initClingoProgramSource();
    runASPSolver();

    Broodwar->printf("Done analyzing map.");
}

Unit* pickBuilder()
{
    // Iterate through all the units that we own
    const std::set<Unit*> myUnits = Broodwar->self()->getUnits();
    for (std::set<Unit*>::const_iterator un = myUnits.begin(); un != myUnits.end(); ++un)
    {
        if ((*un)->getType().isWorker() && (*un)->isConstructing() == false)
        {
            //BWAPI::Broodwar->printf("found one" );
            return *un;
        }
    }

    //BWAPI::Broodwar->printf("Worker Not Found, so, NULL");
    return NULL;
}

void MapAnalysisManager::analyzeChoke()
{
    // choke point analysis
    int x = choke->getCenter().x();
    int y = choke->getCenter().y();
    int maxDist = 10;
    int tileX = x / BTSize;
    int tileY = y / BTSize;

    // analysis of build tiles near choke points
    for (int i = tileX - maxDist; i <= tileX + maxDist; ++i)
    {
        for (int j = tileY - maxDist; j <= tileY + maxDist; ++j)
        {
            // if the tile is in home region
            if (BWTA::getRegion(TilePosition(i, j)) == home)
            {
                // and it is buildable, add it to the buildTiles
                if (Broodwar->isBuildable(TilePosition(i, j)))
                {
                    buildTiles.push_back(std::make_pair(i, j));
                }
            }
            // if the tile is outside the home region
            else
            {
                // and it is walkable, add it to the outside tiles
                if (Broodwar->isWalkable(i * BTSize / WTSize + 1, j * BTSize / WTSize + 2) &&
                    Broodwar->isWalkable(i * BTSize / WTSize + 1, j * BTSize / WTSize + 1) &&
                    Broodwar->isWalkable(i * BTSize / WTSize + 2, j * BTSize / WTSize + 1) &&
                    Broodwar->isWalkable(i * BTSize / WTSize + 2, j * BTSize / WTSize + 2)
                )
                    outsideTiles.push_back(std::make_pair(i, j));
            }
        }
    }

    maxDist += 4;
    for (int i = tileX - maxDist; i <= tileX + maxDist; ++i)
    {
        for (int j = tileY - maxDist; j <= tileY + maxDist; ++j)
        {
            if (Broodwar->isWalkable(i * BTSize / WTSize + 1, j * BTSize / WTSize + 2) &&
                Broodwar->isWalkable(i * BTSize / WTSize + 1, j * BTSize / WTSize + 1) &&
                Broodwar->isWalkable(i * BTSize / WTSize + 2, j * BTSize / WTSize + 1) &&
                Broodwar->isWalkable(i * BTSize / WTSize + 2, j * BTSize / WTSize + 2)
            )
                walkableTiles.push_back(std::make_pair(i, j));
        }
    }


    Unit* builder = pickBuilder();

    assert(builder != NULL);

    // analyze buildable tiles for buildings that we will use to wall in (barracks and supply depot)
    for (unsigned i = 0; i < buildTiles.size(); ++i)
    {
        TilePosition pos(buildTiles[i].first, buildTiles[i].second);

        if (Broodwar->canBuildHere(builder, pos, UnitTypes::Terran_Supply_Depot, false))
            supplyTiles.push_back(std::make_pair(pos.x(), pos.y()));

        if (Broodwar->canBuildHere(builder, pos, UnitTypes::Terran_Barracks, false))
            barracksTiles.push_back(std::make_pair(pos.x(), pos.y()));
    }
}

void MapAnalysisManager::initClingoProgramSource()
{
    std::string writeFile = writeDir + "ITUBotWall.txt";
    std::ofstream file(writeFile.c_str());

    file << "% Building / Unit types" << endl
            << "buildingType(marineType).	" << endl
            << "buildingType(barracksType)." << endl
            << "buildingType(supplyDepotType).	" << endl  << endl

            << "% Size specifications" << endl
            << "width(marineType,1).	height(marineType,1)." << endl
            << "width(barracksType,4).	height(barracksType,3)." << endl
            << "width(supplyDepotType,3). 	height(supplyDepotType,2)." << endl	 << endl

            << "costs(supplyDepotType, 100)." << endl
            << "costs(barracksType, 150)." << endl

            << "% Gaps" << endl
            << "leftGap(barracksType,16). 	rightGap(barracksType,7).	topGap(barracksType,8). 	bottomGap(barracksType,15)." << endl
            << "leftGap(marineType,0). 		rightGap(marineType,0). 	topGap(marineType,0). 		bottomGap(marineType,0)." << endl
            << "leftGap(supplyDepotType,10).		 rightGap(supplyDepotType,9). 	topGap(supplyDepotType,10). 		bottomGap(supplyDepotType,5)." << endl	 << endl

            << "% Facts" << endl
            << "building(marine1).	type(marine1, marineType)." << endl
            << "building(barracks1).	type(barracks1, barracksType)." << endl		
            << "building(barracks2).	type(barracks2, barracksType)." << endl
            << "building(supplyDepot1).	type(supplyDepot1, supplyDepotType)." << endl
            << "building(supplyDepot2).	type(supplyDepot2, supplyDepotType)." << endl 
            << "building(supplyDepot4).	type(supplyDepot4, supplyDepotType)." << endl   
            << "building(supplyDepot3).	type(supplyDepot3, supplyDepotType)." << endl << endl
                                  
            << "% Constraint: two units/buildings cannot occupy the same tile" << endl
            << ":- occupiedBy(B1, X, Y), occupiedBy(B2, X, Y), B1 != B2." << endl	  << endl
                 
            << "% Tiles occupied by buildings" << endl
            << "occupiedBy(B,X2,Y2) :- place(B, X1, Y1)," << endl
            << "						type(B, BT), width(BT,Z), height(BT, Q)," << endl
            << "						X2 >= X1, X2 < X1+Z, Y2 >= Y1, Y2 < Y1+Q," << endl
            << "						walkableTile(X2, Y2)." << endl
            << "						" << endl  << endl

            << "% Gaps between two adjacent tiles, occupied by buildings." << endl
            << "verticalGap(X1,Y1,X2,Y2,G) :-" << endl
            << "	occupiedBy(B1,X1,Y1), occupiedBy(B2,X2,Y2)," << endl
            << "	B1 != B2, X1=X2, Y1=Y2-1, G=S1+S2," << endl
            << "	type(B1,T1), type(B2,T2), bottomGap(T1,S1), topGap(T2,S2)." << endl 
            << "	" << endl
            << "verticalGap(X1,Y1,X2,Y2,G) :-" << endl
            << "	occupiedBy(B1,X1,Y1), occupiedBy(B2,X2,Y2)," << endl
            << "	B1 != B2, X1=X2, Y1=Y2+1, G=S1+S2," << endl
            << "	type(B1,T1), type(B2,T2), bottomGap(T2,S2), topGap(T1,S1)." << endl
            << "	" << endl
            << "horizontalGap(X1,Y1,X2,Y2,G) :-" << endl
            << "	occupiedBy(B1,X1,Y1), occupiedBy(B2,X2,Y2)," << endl
            << "	B1 != B2, X1=X2-1, Y1=Y2, G=S1+S2," << endl
            << "	type(B1,T1), type(B2,T2), rightGap(T1,S1), leftGap(T2,S2)." << endl		 << endl

            << "horizontalGap(X1,Y1,X2,Y2,G) :-" << endl
            << "	occupiedBy(B1,X1,Y1), occupiedBy(B2,X2,Y2)," << endl
            << "	B1 != B2, X1=X2+1, Y1=Y2, G=S1+S2," << endl
            << "	type(B1,T1), type(B2,T2), rightGap(T2,S2), leftGap(T1,S1)." << endl<< endl

            << "cost(B, C) :- place(B, X, Y), type(B, BT), costs(BT, COST), C=COST." << endl

            ///////////////
            << "% Tile information" << endl;

            for(std::vector<std::pair<int, int> >::const_iterator it = walkableTiles.begin();
                it != walkableTiles.end(); ++it){
                file << "walkableTile(" << it->first << ", " << it->second << ")." << endl;
            }

            for(std::vector<std::pair<int, int> >::const_iterator it = barracksTiles.begin();
                it != barracksTiles.end(); ++it){
                file << "buildable(barracksType, " << it->first << ", " << it->second << ")." << endl;
            }

            for(std::vector<std::pair<int, int> >::const_iterator it = supplyTiles.begin();
                it != supplyTiles.end(); ++it){
                file << "buildable(supplyDepotType, " << it->first << ", " << it->second << ")." << endl;
            }
            ////////////////////////

            insideBase = findClosestTile(buildTiles);
            outsideBase = findFarthestTile(outsideTiles);
            file << endl << "insideBase(" << insideBase.first << ", " << insideBase.second << ").\t";
            file << "outsideBase(" << outsideBase.first << ", " << outsideBase.second << ")." << endl << endl


            << "% Constraint: Inside of the base must not be reachable." << endl
            << ":- insideBase(X2,Y2), outsideBase(X1,Y1), canReach(X2,Y2)." << endl	<< endl

            << "% Reachability between tiles." << endl
            << "blocked(X,Y) :- occupiedBy(B,X,Y), building(B), walkableTile(X,Y)." << endl
            << "canReach(X,Y) :- outsideBase(X,Y)." << endl	 << endl

            << "canReach(X2,Y) :-" << endl
            << "	canReach(X1,Y), X1=X2+1, walkableTile(X1,Y), walkableTile(X2,Y)," << endl
            << "	not blocked(X1,Y), not blocked(X2,Y)." << endl
            << "canReach(X2,Y) :-" << endl
            << "	canReach(X1,Y), X1=X2-1, walkableTile(X1,Y), walkableTile(X2,Y)," << endl
            << "	not blocked(X1,Y), not blocked(X2,Y)." << endl
            << "canReach(X,Y2) :-" << endl
            << "	canReach(X,Y1), Y1=Y2+1, walkableTile(X,Y1), walkableTile(X,Y2)," << endl
            << "	not blocked(X,Y1), not blocked(X,Y2)." << endl
            << "canReach(X,Y2) :-" << endl
            << "	canReach(X,Y1), Y1=Y2-1, walkableTile(X,Y1), walkableTile(X,Y2)," << endl
            << "	not blocked(X,Y1), not blocked(X,Y2)." << endl
            << "canReach(X2,Y2) :-" << endl
            << "	canReach(X1,Y1), X1=X2+1, Y1=Y2+1, walkableTile(X1,Y1), walkableTile(X2,Y2)," << endl
            << "	not blocked(X1,Y1), not blocked(X2,Y2)." << endl
            << "canReach(X2,Y2) :-" << endl
            << "	canReach(X1,Y1), X1=X2-1, Y1=Y2+1, walkableTile(X1,Y1), walkableTile(X2,Y2)," << endl
            << "	not blocked(X1,Y1), not blocked(X2,Y2)." << endl
            << "canReach(X2,Y2) :-" << endl
            << "	canReach(X1,Y1), X1=X2+1, Y1=Y2-1, walkableTile(X1,Y1), walkableTile(X2,Y2)," << endl
            << "	not blocked(X1,Y1), not blocked(X2,Y2)." << endl
            << "canReach(X2,Y2) :-" << endl
            << "	canReach(X1,Y1), X1=X2-1, Y1=Y2-1, walkableTile(X1,Y1), walkableTile(X2,Y2)," << endl
            << "	not blocked(X1,Y1), not blocked(X2,Y2)." << endl	   << endl

            << "% Using gaps to reach (walk on) blocked locations." << endl
            << "enemyUnitX(32). enemyUnitY(32)." << endl
            << "canReach(X1,Y1) :- horizontalGap(X1,Y1,X2,Y1,G), G >= S, X2=X1+1, canReach(X1,Y3), Y3=Y1+1, enemyUnitX(S)." << endl
            << "canReach(X1,Y1) :- horizontalGap(X1,Y1,X2,Y1,G), G >= S, X2=X1-1, canReach(X1,Y3), Y3=Y1+1, enemyUnitX(S)." << endl
            << "canReach(X1,Y1) :- horizontalGap(X1,Y1,X2,Y1,G), G >= S, X2=X1+1, canReach(X1,Y3), Y3=Y1-1, enemyUnitX(S)." << endl
            << "canReach(X1,Y1) :- horizontalGap(X1,Y1,X2,Y1,G), G >= S, X2=X1-1, canReach(X1,Y3), Y3=Y1-1, enemyUnitX(S)." << endl
            << "canReach(X1,Y1) :- verticalGap(X1,Y1,X1,Y2,G), G >= S, Y2=Y1+1, canReach(X3,Y1), X3=X1-1, enemyUnitY(S)." << endl
            << "canReach(X1,Y1) :- verticalGap(X1,Y1,X1,Y2,G), G >= S, Y2=Y1-1, canReach(X3,Y1), X3=X1-1, enemyUnitY(S)." << endl
            << "canReach(X1,Y1) :- verticalGap(X1,Y1,X1,Y2,G), G >= S, Y2=Y1+1, canReach(X3,Y1), X3=X1+1, enemyUnitY(S)." << endl
            << "canReach(X1,Y1) :- verticalGap(X1,Y1,X1,Y2,G), G >= S, Y2=Y1-1, canReach(X3,Y1), X3=X1+1, enemyUnitY(S)." << endl

            //<< ":- place(supplyDepot2, X, Y) | place(barracks2, X, Y)." << endl			// error: |
            //<< ":- not place(supplyDepot2, X, Y), not place(barracks2, X, Y)." << endl	// unsafe variables X & Y

            << "% Generate all the potential placements." << endl
            << "1[place(barracks1,X,Y) : buildable(barracksType,X,Y)]1." << endl 
            << "0[place(barracks2,X,Y) : buildable(barracksType,X,Y)]1." << endl 

            << "1[place(supplyDepot1,X,Y) : buildable(supplyDepotType,X,Y)]1." << endl
            << "0[place(supplyDepot2,X,Y) : buildable(supplyDepotType,X,Y)]1." << endl
            << "0[place(supplyDepot3,X,Y) : buildable(supplyDepotType,X,Y)]1." << endl	    
            << "0[place(supplyDepot4,X,Y) : buildable(supplyDepotType,X,Y)]1." << endl << endl
                                                                  
            << "% Optimization criterion" << endl;	

            if(optimizeGap){
                file << "#minimize [horizontalGap(X1,Y1,X2,Y2,G) = G @1 ]." << endl	
                << "#minimize [verticalGap(X1,Y1,X2,Y2,G) = G @1 ]." << endl
                << "#minimize [place(supplyDepot2,X,Y) @2]." << endl       
                << "#minimize [place(supplyDepot3,X,Y) @2]." << endl	
                << "#minimize [place(supplyDepot4,X,Y) @2]." << endl	
                << "#minimize [place(barracks2,X,Y) @2]. " << endl	 << endl;	 
            }
            else
                file << "#minimize [cost(B, C) = C]." << endl;


            file << "#hide." << endl
            << "#show place/3." << endl;



        BWAPI::Broodwar->printf("ASP Solver Contents Ready!");

        optimizeGap ? BWAPI::Broodwar->printf("Optimization: GAP") 
                    : BWAPI::Broodwar->printf("Optimization: COST");

        file.close();
}

void MapAnalysisManager::runASPSolver()
{
    std::string clingo = "bwapi-data\\AI\\clingo.exe ";
    std::string problemPart = "ITUBotWall.txt > ";
    std::string outputPart = "out.txt";

    std::string combined = (clingo + writeDir + problemPart + writeDir + outputPart);

    Broodwar->printf(combined.c_str());

    system(combined.c_str());

    std::vector<std::string> lines;
    std::string line;
    unsigned lineCounter = 0;
    std::ifstream file((writeDir + outputPart).c_str());
    if (file.is_open())
    {
        while (getline(file, line))
        {
            if (*(line.end() - 1) == '\r')
                line.erase(line.end() - 1);
            lines.push_back(line);
            if (line == "OPTIMUM FOUND")
            {
                line = lines[lineCounter - 2]; // contains final answer that will be parsed
                break;
            }
            if (line == "UNSATISFIABLE")
            { // error in solver
                Broodwar->printf("Solver failed finding a solution!");
                return;
            }
            lineCounter++;
        }

        // parse the answer, example output below
        // place(supplyDepot1,119,46) place(supplyDepot2,122,44) place(barracks1,116,52) 
        std::stringstream ss;
        std::string token;
        while (line != "")
        { // tokenizin the whole line
            std::vector<int> coords;
            UnitType type;
            int val;


            ss << line.substr(6, line.find(")") - 6); // place( = 6 chars
            while (getline(ss, token, ','))
            { // tokenizing the individual place() statements
                std::istringstream iss(token);
                iss >> val;

                if (iss.fail())
                {
                    size_t found = token.find("supplyDepot"); // search for supply depot

                    // if found its supply depot, if not found its barracks (early wall)
                    type = found != std::string::npos ? UnitTypes::Terran_Supply_Depot : UnitTypes::Terran_Barracks;
                }
                else
                { // coordinates
                    coords.push_back(val);
                }
            }

            // erase the parsed part including the closing parenthesis and space
            line.erase(0, line.find(")") + 2);

            // clear buffers
            ss.clear();
            token.clear();

            // add the result to data structure after successful parsing for each place() statement
            wallLayout.push_back(std::make_pair(type, TilePosition(coords[0], coords[1])));
        }

        // save results into bots memory
        _wall = wallLayout;

        file.close();

        // finally, add choke point width to the output file
        std::ofstream oFile("D:/SCAI/IT_WORKS/StarCraft/bwapi-data/AI/out.txt", std::ios::app);

        if (oFile.is_open())
        {
            oFile << "Choke Width: " << choke->getWidth() << endl;
            oFile.close();
        }
        else
        {
            Broodwar->printf("Error opening output file");
        }
    }
    else
        Broodwar->printf("** ERROR OPENING SOLVER OUTPUT FILE");
}

std::pair<int, int> findClosestTile(const std::vector<std::pair<int, int>>& tiles)
{
    std::pair<int, int> ret;
    Position p;
    std::set<Unit*> units = Broodwar->self()->getUnits();

    for (std::set<Unit*>::const_iterator u = units.begin(); u != units.end(); ++u)
    {
        if ((*u)->getType() == UnitTypes::Terran_Command_Center)
        {
            p = (*u)->getPosition();
            break;
        }
    }

    double dist = 9000000000;

    for (std::vector<std::pair<int, int>>::const_iterator it = walkableTiles.begin(); it != walkableTiles.end(); ++it)
    {
        if (p.getDistance(Position(it->first * BTSize, it->second * BTSize)) <= dist &&
            p.hasPath(Position(it->first * BTSize, it->second * BTSize))
        )
        {
            dist = p.getDistance(Position(it->first * BTSize, it->second * BTSize));
            ret = *it;
        }
    }

    //ret.first = p.x()/BTSize; 
    //ret.second = p.y()/BTSize;
    return ret;
}

std::pair<int, int> findFarthestTile(const std::vector<std::pair<int, int>>& tiles)
{
    std::pair<int, int> ret;

    // get the position of the command center
    Position p;
    std::set<Unit*> units = Broodwar->self()->getUnits();
    for (std::set<Unit*>::const_iterator u = units.begin(); u != units.end(); ++u)
    {
        if ((*u)->getType() == UnitTypes::Terran_Command_Center)
        {
            p = (*u)->getPosition();
            break;
        }
    }

    double dist = 0;

    // pick the farthest tile
    for (std::vector<std::pair<int, int>>::const_iterator it = tiles.begin(); it != tiles.end(); ++it)
    {
        if (p.getDistance(Position(it->first * BTSize, it->second * BTSize)) >= dist &&
            p.hasPath(Position(it->first * BTSize, it->second * BTSize))
        )
        {
            dist = p.getDistance(Position(it->first * BTSize, it->second * BTSize));
            ret = *it;
        }
    }

    return ret;
}