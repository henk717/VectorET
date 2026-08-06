/*
 * OBCommonEmscripten.cpp
 *
 * Emscripten "unity" translation unit for the Omni-Bot Common library.
 *
 * This mirrors the include order of vendor/omni-bot/0.83/Omnibot/Common/
 * BatchBuild.cpp exactly (order matters for a unity build), but omits the
 * translation units that cannot or should not run under WebAssembly:
 *
 *   - DebugWindow.cpp / gmDebugWindow.cpp : native Win32/GUI debug overlay
 *   - FileDownloader.cpp                  : boost::asio / boost::thread
 *   - Interprocess.cpp                    : boost::interprocess message queues
 *   - mdump.cpp                           : Win32 mini-dump handler
 *
 * None of these are referenced by the in-game bot logic; the network/IPC
 * bits are additionally guarded behind ENABLE_FILE_DOWNLOADER in the
 * precompiled header, which this build never defines.
 *
 * BotExports.cpp is NOT included here - it is compiled by the game-specific
 * module (ET) so the single OMNIBOT_API entry point is exported from the
 * final side module.
 */

#include "PrecompCommon.h"
#include "PrecompCommon.cpp"

#include "Client.cpp"
#include "IGame.cpp"
#include "IGameManager.cpp"

#include "BlackBoard.cpp"
#include "BlackBoardItems.cpp"

// [emscripten] DebugWindow.cpp omitted (native GUI)

#include "Omni-Bot.cpp"

#include "EventReciever.cpp"

#include "GoalManager.cpp"
#include "MapGoal.cpp"
#include "MapGoalDatabase.cpp"

// misc
#include "CallbackParameters.cpp"
#include "CommandReciever.cpp"
#include "Criteria.cpp"
#include "EngineFuncs.cpp"
// [emscripten] FileDownloader.cpp omitted (boost::asio/thread)
#include "InterfaceFuncs.cpp"
// [emscripten] Interprocess.cpp omitted (boost::interprocess IPC)
#include "KeyValueIni.cpp"
#include "Logger.cpp"
#include "NameManager.cpp"
#include "PropertyBinding.cpp"
#include "Regulator.cpp"
#include "Timer.cpp"
#include "Trajectory.cpp"
#include "TriggerManager.cpp"
#include "Utilities.cpp"

#if ENABLE_PATH_PLANNERS
#include "PathPlannerRecast.cpp"
#include "PathPlannerRecastCmds.cpp"
#include "PathPlannerRecastScript.cpp"

#include "PathPlannerFloodFill.cpp"
#include "PathPlannerFloodFillBuilder.cpp"
#include "PathPlannerFloodFillCmds.cpp"
#include "PathPlannerFloodFillScript.cpp"

#include "PathPlannerNavMesh.cpp"
#include "PathPlannerNavMeshBuilder.cpp"
#include "PathPlannerNavMeshCmds.cpp"
#include "PathPlannerNavMeshScript.cpp"
#include "QuadTree.cpp"
#endif

#include "PathPlannerWaypoint.cpp"
#include "PathPlannerWaypointCmds.cpp"
#include "PathPlannerWaypointScript.cpp"
#include "Waypoint.cpp"
#include "WaypointSerializer_V1.cpp"
#include "WaypointSerializer_V2.cpp"
#include "WaypointSerializer_V3.cpp"
#include "WaypointSerializer_V4.cpp"
#include "WaypointSerializer_V5.cpp"
#include "WaypointSerializer_V6.cpp"
#include "WaypointSerializer_V7.cpp"
#include "WaypointSerializer_V9.cpp"

#include "NavigationManager.cpp"
#include "Path.cpp"
#include "PathPlannerBase.cpp"

#include "gmBotLibrary.cpp"
#include "gmMathLibrary.cpp"
#include "gmSystemLibApp.cpp"
#include "gmUtilityLib.cpp"
#include "ScriptManager.cpp"

#include "gmAABB.cpp"
#include "gmBot.cpp"
// [emscripten] gmDebugWindow.cpp omitted (native GUI)
#include "gmGameEntity.cpp"
#include "gmMatrix3.cpp"
#include "gmNamesList.cpp"
#include "gmScriptGoal.cpp"
#include "gmTargetInfo.cpp"
#include "gmTimer.cpp"
#include "gmTriggerInfo.cpp"
#include "gmWeapon.cpp"

#include "StateMachine.cpp"
#include "BotBaseStates.cpp"
#include "BotSteeringSystem.cpp"
#include "BotWeaponSystem.cpp"
#include "FilterAllType.cpp"
#include "FilterClosest.cpp"
#include "FilterSensory.cpp"
#include "ScriptGoal.cpp"

#include "BotSensoryMemory.cpp"
#include "BotTargetingSystem.cpp"
#include "MemoryRecord.cpp"
#include "TargetInfo.cpp"

#include "Weapon.cpp"
#include "WeaponDatabase.cpp"

#include "FileSystem.cpp"

#include "RenderOverlay.cpp"
#include "RenderOverlayGame.cpp"

// [emscripten] mdump.cpp omitted (Win32 crash dumps)
