/*
    Rainbomizer - A (probably fun) Grand Theft Auto San Andreas Mod that
                  randomizes stuff
    Copyright (C) 2019 - Parik

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

 */

#include "traffic.hh"
#include "logger.hh"
#include "functions.hh"
#include <windows.h>
#include <ctime>
#include "injector/injector.hpp"
#include "loader.hh"
#include "config.hh"

const int          LOG_MOST_RECENT_VEHICLES     = 5;
TrafficRandomizer *TrafficRandomizer::mInstance = nullptr;

/*******************************************************/
void
TrafficRandomizer::Initialise ()
{

    auto config = ConfigManager::GetInstance ()->GetConfigs ().traffic;

    if (!config.enabled)
        return;

    if (config.forcedVehicleEnabled && config.forcedVehicleID >= 400
        && config.forcedVehicleID <= 611)
        this->SetForcedRandomCar (config.forcedVehicleID);

    RegisterHooks ({{HOOK_JUMP, 0x421980, (void *) &RandomizePoliceCars},
                    {HOOK_CALL, 0x431EE5, (void *) &FixEmptyPoliceCars},
                    {HOOK_CALL, 0x43022A, (void *) &RandomizeTrafficCars},
                    {HOOK_CALL, 0x613B7F, (void *) &RandomizeCarPeds},
                    {HOOK_JUMP, 0x421900, (void *) &RandomizeCarToLoad},
                    {HOOK_CALL, 0x42C620, (void *) &FixEmptyPoliceCars},
                    {HOOK_CALL, 0x501F3B, (void *) &FixFreightTrainCrash}});

    ExceptionManager::GetExceptionManager ()->RegisterHandler (
        &ExceptionHandlerCallback);

    Logger::GetLogger ()->LogMessage ("Registered Traffic Randomizer");

    FixTrainSpawns ();
}

/*******************************************************/
/* Fixes train spawns by making them spawn as a boat */
/*******************************************************/
void
TrafficRandomizer::FixTrainSpawns ()
{
    int *GNVJumpTable               = (int *) (0x42170C);
    GNVJumpTable[VEHICLE_TRAIN - 1] = GNVJumpTable[VEHICLE_BOAT - 1];
}

/*******************************************************/
void
TrafficRandomizer::DestroyInstance ()
{
    if (TrafficRandomizer::mInstance)
        delete TrafficRandomizer::mInstance;
}

/*******************************************************/
TrafficRandomizer *
TrafficRandomizer::GetInstance ()
{
    if (!TrafficRandomizer::mInstance)
        {
            TrafficRandomizer::mInstance = new TrafficRandomizer ();
            atexit (&TrafficRandomizer::DestroyInstance);
        }
    return TrafficRandomizer::mInstance;
}

/*******************************************************/
void
TrafficRandomizer::ExceptionHandlerCallback (_EXCEPTION_POINTERS *ep)
{
    auto inst = GetInstance ();

    std::string log = "Last loaded vehicles: ";
    for (auto vehicle : inst->mMostRecentLoadedVehicles)
        {
            log += std::to_string (vehicle);
            log += " ";
        }
    log += "\nLast spawned vehicles: ";
    for (auto vehicle : inst->mMostRecentSpawnedVehicles)
        {
            log += std::to_string (vehicle);
            log += " ";
        }

    Logger::GetLogger ()->LogMessage (log);
}

/*******************************************************/
/* Chooses a random vehicle for police cars else falls
 back to the default one  */
/*******************************************************/
int
RandomizePoliceCars ()
{
    int model = RandomizeTrafficCars (nullptr);
    return model == -1 ? CStreaming::GetDefaultCopCarModel (0) : model;
}

/*******************************************************/
/* Chooses a random vehicle for traffic vehicles */
/*******************************************************/
void
LoadRandomVehiclesAtStart ()
{
    auto trafficRandomizer = TrafficRandomizer::GetInstance ();

    if (!trafficRandomizer->mInitialVehiclesLoaded)
        {
            // Randomizes loading a few vehicles
            for (int i = 0; i < 5; i++)
                {
                    int model = RandomizeCarToLoad ();
                    if (model != -1)
                        CStreaming::RequestModel (model, 8);
                }

            CStreaming::LoadAllRequestedModels (false);
            trafficRandomizer->mInitialVehiclesLoaded = true;
        }
}

/*******************************************************/
/* Chooses a random vehicle for traffic vehicles */
/*******************************************************/

int
RandomizeTrafficCars (int *type)
{
    auto trafficRandomizer = TrafficRandomizer::GetInstance ();

    int random_id = StreamingManager::GetRandomLoadedVehicle ();

    if (trafficRandomizer->mForcedCar)
        random_id = trafficRandomizer->mForcedCar;

    else if (!trafficRandomizer->IsVehicleAllowed (random_id))
        return -1;

    if (ms_aInfoForModel[random_id].m_nLoadState == 1)
        {
            // Add to the recently spawned list
            trafficRandomizer->mMostRecentSpawnedVehicles.push_back (random_id);

            if (trafficRandomizer->mMostRecentSpawnedVehicles.size ()
                > LOG_MOST_RECENT_VEHICLES)
                trafficRandomizer->mMostRecentSpawnedVehicles.pop_front ();

            // Set type for vehicles
            if (type)
                {
                    CCarCtrl::ChooseModel (type);
                    if (CModelInfo::IsPoliceModel (random_id))
                        *type = 13; // TYPE_POLICECAR
                }
            return random_id;
        }
    else if (trafficRandomizer->mForcedCar)
        return -1;

    return -1;
}

/*******************************************************/
/* Returns true/false if config file allows a vehicle to spawn */
/*******************************************************/
bool
TrafficRandomizer::IsVehicleAllowed (int model)
{
    auto config = ConfigManager::GetInstance ()->GetConfigs ().traffic;

    // Aircrafts
    if ((CModelInfo::IsHeliModel (model) || CModelInfo::IsPlaneModel (model))
        && !config.enableAircrafts)
        return false;

    // Boats
    if (CModelInfo::IsBoatModel (model) && !config.enableBoats)
        return false;

    // Bikes
    if ((CModelInfo::IsBikeModel (model) || CModelInfo::IsBmxModel (model))
        && !config.enableBikes)
        return false;

    // Trains
    if (CModelInfo::IsTrainModel (model) && !config.enableTrains)
        return false;

    // Cars
    if ((CModelInfo::IsCarModel (model) || CModelInfo::IsQuadBikeModel (model)
         || CModelInfo::IsMonsterTruckModel (model))
        && !config.enableCars)
        return false;

    // Trailers
    if (CModelInfo::IsTrailerModel (model) && !config.enableTrailers)
        return false;

    return true;
}

/*******************************************************/
/* Chooses a random vehicle for loading */
/*******************************************************/
int
RandomizeCarToLoad ()
{
    auto trafficRandomizer = TrafficRandomizer::GetInstance ();
    for (int i = 0; i < 16; i++)
        {
            int random_id = random (611, 400);
            if (!trafficRandomizer->IsVehicleAllowed (random_id))
                continue;

            if (trafficRandomizer->mForcedCar)
                random_id = trafficRandomizer->mForcedCar;

            if (ms_aInfoForModel[random_id].m_nLoadState != 1
                || trafficRandomizer->mForcedCar)
                {
                    // Add to the recently spawned list
                    trafficRandomizer->mMostRecentLoadedVehicles.push_back (
                        random_id);

                    if (trafficRandomizer->mMostRecentLoadedVehicles.size ()
                        > LOG_MOST_RECENT_VEHICLES)
                        trafficRandomizer->mMostRecentLoadedVehicles
                            .pop_front ();
                    return random_id;
                }
        }
    return -1;
}

/*******************************************************/
/* Fixes no peds spawning in police vehicle by fooling the game
   into thinking that it is a different vehicle (temporarily)
  */
/*******************************************************/
void
FixEmptyPoliceCars (uint8_t *vehicle, char a3)
{
    uint16_t *modelIndex     = (uint16_t *) (vehicle + 0x22);
    uint16_t  original_index = *modelIndex;

    *modelIndex = ChoosePoliceVehicleBasedOnModel (original_index);
    CCarAI::AddPoliceCarOccupants (vehicle, a3);

    *modelIndex = original_index; // restore original model
}

/*******************************************************/
/* Fixes crashes when the game spawns peds that haven't been loaded yet
   Randomized peds later.
  */
/*******************************************************/
void *
RandomizeCarPeds (int type, int model, float *pos, bool unk)
{

	printf("%d\n", model);
    if (ms_aInfoForModel[model].m_nLoadState == 1)
        return CPopulation::AddPed (type, model, pos, unk);

    auto config = ConfigManager::GetInstance ()->GetConfigs ().traffic;

    // spawn CJ because why not :P
    return CPopulation::AddPed (type, config.defaultModel, pos, unk);
}

/*******************************************************/
/* Fixes crashes when the game spawns peds that haven't been loaded yet
   Randomized peds later.
  */
/*******************************************************/
void __fastcall FixFreightTrainCrash (CAEVehicleAudioEntity *audio, void *edx,
                                      cVehicleParams *vehicle_params)
{
    uint32_t vehicleSubClass = vehicle_params->m_nVehicleSubclass;
    uint8_t *vehicle         = (uint8_t *) vehicle_params->m_pVehicle;

    // Trains are spawned as boat in the ASI
    if (vehicleSubClass == VEHICLE_BOAT)
        {
            uint16_t modelId = *reinterpret_cast<uint16_t *> (vehicle + 0x22);

            const int MODEL_FLAT_FREIGHT = 569;
            const int MODEL_FREIGHT      = 537;
            if (modelId == MODEL_FLAT_FREIGHT || modelId == MODEL_FREIGHT)
                return;
        }

    audio->ProcessTrainTrackSound (vehicle_params);
}

/*******************************************************/
int
ChoosePoliceVehicleBasedOnModel (int model)
{
    if (model != 528 && model != 601 && CModelInfo::IsPoliceModel (model))
        return model;

    int seats
        = CModelInfo::GetMaximumNumberOfPassengersFromNumberOfDoors (model);
    switch (seats)
        {
        case 1: return 523;

        case 2: return 599;

        default: return 490;
        };
}
