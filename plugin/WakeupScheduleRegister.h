/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __WAKEUP_SCHEDULE_REGISTER_H__
#define __WAKEUP_SCHEDULE_REGISTER_H__

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include "UtilsLogging.h"

#define DEBUG_LOG LOGINFO
#define INFO_LOG LOGINFO
#define ERROR_LOG LOGERR

class WakeupScheduleRegister
{
  public:

    typedef unsigned int UnixTime;

    enum PowerState
    {
        Operational,
        ActiveStandby
    };

    enum OperationStatus
    {
        Successful,
        Failed
    };

    struct NearestWakeupSchedule
    {
        UnixTime unixTime;
        PowerState powerState;
        std::vector<std::string> requestorIds;
    };

    OperationStatus addWakeupSchedule(UnixTime unixTime, PowerState powerState, const char* requestorId = NULL)
    {
        OperationStatus status = Failed;

        if (!requestorId || !requestorId[0])
        {
            requestorId = "anonymous";
        }

        if (isAlphaNumeric(requestorId))
        {
            std::vector<Schedule*>::iterator it = findUnixTime(unixTime);

            if (it != wakeupSchedules.end())
            {
                std::vector<std::string>::const_iterator rIt = (*it)->requestorIds.begin();
                std::vector<PowerState>::const_iterator pIt = (*it)->powerStates.begin();
                bool found = false;

                for (; rIt != (*it)->requestorIds.end() && pIt != (*it)->powerStates.end(); rIt++, pIt++)
                {
                    if (*rIt == requestorId && *pIt == powerState)
                    {
                        found = true;
                        break;
                    }
                }

                if (!found)
                {
                    (*it)->requestorIds.push_back(requestorId);
                    (*it)->powerStates.push_back(powerState);
                }

                status = Successful;
            }
            else
            {
                Schedule* schedule = new Schedule(unixTime, powerState, requestorId);
                it = wakeupSchedules.insert(std::lower_bound(wakeupSchedules.begin(),
                    wakeupSchedules.end(), schedule, lessSchedule), schedule);

                if (it != wakeupSchedules.end())
                {
                    status = Successful;
                }
                else
                {
                    delete schedule;
                }
            }
        }

        DEBUG_LOG("%s(): unixTime = %u, powerState = '%s', requestorId = '%s', status = %d\n",
            __FUNCTION__, unixTime, powerStateToString(powerState), requestorId, status);

        return status;
    }

    NearestWakeupSchedule* peekNearestWakeupSchedule()
    {
        NearestWakeupSchedule* result = NULL;

        if (!wakeupSchedules.empty() && !wakeupSchedules[0]->powerStates.empty())
        {
            const PowerState priority[] = {Operational, ActiveStandby};

            for (unsigned int i = 0; i < sizeof(priority) / sizeof(priority[0]) ; i++)
            {
                std::vector<PowerState>::iterator it = find(wakeupSchedules[0]->powerStates.begin(),
                    wakeupSchedules[0]->powerStates.end(), priority[i]);

                if (it != wakeupSchedules[0]->powerStates.end())
                {
                    result = new NearestWakeupSchedule();

                    if (result)
                    {
                        std::vector<std::string>::const_iterator rIt = wakeupSchedules[0]->requestorIds.begin();
                        std::vector<PowerState>::const_iterator pIt = wakeupSchedules[0]->powerStates.begin();
                        result->unixTime = wakeupSchedules[0]->unixTime;
                        result->powerState = *it;

                        for (; rIt != wakeupSchedules[0]->requestorIds.end() && pIt != wakeupSchedules[0]->powerStates.end(); rIt++, pIt++)
                        {
                            if (*pIt == *it)
                            {
                                result->requestorIds.push_back(*rIt);
                            }
                        }
                    }
                    else
                    {
                        ERROR_LOG("%s(): Memory allocation error!\n", __FUNCTION__);
                    }

                    break;
                }
            }
        }

        dumpSchedules(pastWakeupSchedules, "past");
        dumpSchedules(wakeupSchedules, "future");

        if (result)
        {
            INFO_LOG("%s(): unixTime = %u, powerState = '%s', requestorIds = '%s'\n",
                __FUNCTION__, result->unixTime, powerStateToString(result->powerState),
                vectorToString(result->requestorIds).c_str());
        }
        else
        {
            INFO_LOG("%s(): result = %p\n", __FUNCTION__, result);
        }

        return result;
    }

    NearestWakeupSchedule* getNearestWakeupSchedule()
    {
        removeExpiredSchedules();
        return peekNearestWakeupSchedule();
    }

    std::pair<std::vector<std::pair<UnixTime, PowerState> >, std::vector<std::pair<UnixTime, PowerState> > >
        getPastAndFutureWakeupSchedules(const char* requestorId = NULL)
    {
        std::vector<std::pair<UnixTime, PowerState> > pastSchedules;
        std::vector<std::pair<UnixTime, PowerState> > futureSchedules;
        std::vector<std::pair<UnixTime, PowerState> >::iterator it;
        size_t i;

        removeExpiredSchedules();
        dumpSchedules(pastWakeupSchedules, "past");
        dumpSchedules(wakeupSchedules, "future");

        if (requestorId)
        {
            pastSchedules = getSchedulesByRequestorId(pastWakeupSchedules, requestorId);
            futureSchedules = getSchedulesByRequestorId(wakeupSchedules, requestorId);

            for (it = pastSchedules.begin(), i = 1; it != pastSchedules.end(); it++, i++)
            {
                DEBUG_LOG("%s(): past: unixTime = %u, powerState = '%s', requestorId = '%s' (%zu/%zu)\n",
                    __FUNCTION__, it->first, powerStateToString(it->second), requestorId, i, pastSchedules.size());
            }

            for (it = futureSchedules.begin(), i = 1; it != futureSchedules.end(); it++, i++)
            {
                DEBUG_LOG("%s(): future: unixTime = %u, powerState = '%s', requestorId = '%s' (%zu/%zu)\n",
                    __FUNCTION__, it->first, powerStateToString(it->second), requestorId, i, futureSchedules.size());
            }
        }
        else
        {
            pastSchedules = getPrioritizedSchedules(pastWakeupSchedules);
            futureSchedules = getPrioritizedSchedules(wakeupSchedules);

            for (it = pastSchedules.begin(), i = 1; it != pastSchedules.end(); it++, i++)
            {
                DEBUG_LOG("%s(): past: unixTime = %u, powerState = '%s' (%zu/%zu)\n",
                    __FUNCTION__, it->first, powerStateToString(it->second), i, pastSchedules.size());
            }

            for (it = futureSchedules.begin(), i = 1; it != futureSchedules.end(); it++, i++)
            {
                DEBUG_LOG("%s(): future: unixTime = %u, powerState = '%s' (%zu/%zu)\n",
                    __FUNCTION__, it->first, powerStateToString(it->second), i, futureSchedules.size());
            }
        }

        return std::make_pair(pastSchedules, futureSchedules);
    }

    OperationStatus removeWakeupSchedule(UnixTime unixTime, PowerState powerState, const char* requestorId = NULL)
    {
        std::vector<Schedule*>::iterator it = findUnixTime(unixTime);
        OperationStatus status = Failed;

        if (!requestorId)
        {
            requestorId = "anonymous";
        }

        if (it != wakeupSchedules.end())
        {
            for (size_t i = 0; i < (*it)->powerStates.size() && i < (*it)->requestorIds.size(); i++)
            {
                if ((*it)->powerStates[i] == powerState && (*it)->requestorIds[i] == requestorId)
                {
                    (*it)->powerStates.erase((*it)->powerStates.begin() + i);
                    (*it)->requestorIds.erase((*it)->requestorIds.begin() + i);

                    if ((*it)->powerStates.empty() && (*it)->requestorIds.empty())
                    {
                        delete (*it);
                        wakeupSchedules.erase(it);
                    }

                    status = Successful;
                    break;
                }
            }
        }

        DEBUG_LOG("%s(): unixTime = %u, powerState = '%s', requestorId = '%s', status = %d\n",
            __FUNCTION__, unixTime, powerStateToString(powerState), requestorId, status);

        return status;
    }

    OperationStatus removeAllWakeupSchedules()
    {
        if (!wakeupSchedules.empty())
        {
            clearSchedules(wakeupSchedules);
            DEBUG_LOG("%s(): all future schedules removed\n", __FUNCTION__);
        }

        if (!pastWakeupSchedules.empty())
        {
            clearSchedules(pastWakeupSchedules);
            DEBUG_LOG("%s(): all past scheduls removed\n", __FUNCTION__);
        }

        return Successful;
    }

    OperationStatus loadWakeupSchedulesFromFile(const char* path)
    {
        OperationStatus status = Failed;

        if (!path)
        {
            return status;
        }

        FILE* file = fopen(path, "r");

        if (!file)
        {
            INFO_LOG("%s(): file does not exist or cannot be opened, path = '%s'", __FUNCTION__, path);
            clearSchedules(wakeupSchedules);
            clearSchedules(pastWakeupSchedules);
            return Successful;
        }

        fseek(file, 0, SEEK_END);
        long fileSize = ftell(file);
        fseek(file, 0, SEEK_SET);

        if (fileSize <= 0)
        {
            clearSchedules(wakeupSchedules);
            clearSchedules(pastWakeupSchedules);
            fclose(file);
            return Successful;
        }

        std::string data(fileSize + 1, '\0');
        size_t bytesRead = fread(&data[0], 1, fileSize, file);
        fclose(file);

        if (bytesRead != static_cast<size_t>(fileSize))
        {
            ERROR_LOG("%s(): failed to read file, path = '%s', bytesRead = %zu, fileSize = %ld",
                __FUNCTION__, path, bytesRead, fileSize);
            return status;
        }

        std::vector<Schedule*> schedules;

        if (data.compare(0, sizeof("{v:1}[") - 1, "{v:1}[") != 0)
        {
            ERROR_LOG("%s(): invalid file format, path = '%s'", __FUNCTION__, path);
            return status;
        }

        size_t i = sizeof("{v:1}[") - 1;

        if (i < data.size())
        {
            if (data[i] != ']')
            {
                while (i < data.size() && data[i] == '{')
                {
                    size_t j = data.find('}', i + 1);

                    if (j != std::string::npos)
                    {
                        Schedule* schedule = stringToSchedule(data.substr(i, (j + 1) - i));

                        if (schedule->unixTime && schedule->requestorIds.size())
                        {
                            schedules.push_back(schedule);
                            i = j + 1;

                            if (i < data.size())
                            {
                                if (data[i] == ',')
                                {
                                    i++;
                                }
                                else
                                {
                                    if (data[i] == ']')
                                    {
                                        status = Successful;
                                    }
                                    break;
                                }
                            }
                            else
                            {
                                break;
                            }
                        }
                        else
                        {
                            delete schedule;
                            break;
                        }
                    }
                    else
                    {
                        break;
                    }
                }
            }
            else
            {
                status = Successful;
            }
        }

        if (status == Successful)
        {
            clearSchedules(wakeupSchedules);
            clearSchedules(pastWakeupSchedules);

            wakeupSchedules = schedules;

            removeExpiredSchedules();
            dumpSchedules(pastWakeupSchedules, "past");
            dumpSchedules(wakeupSchedules, "future");

            INFO_LOG("%s(): path = '%s', past = %zu, future = %zu, status = %d",
                __FUNCTION__, path, pastWakeupSchedules.size(), wakeupSchedules.size(), status);
        }
        else
        {
            clearSchedules(schedules);
            ERROR_LOG("%s(): path = '%s', status = %d", __FUNCTION__, path, status);
        }

        return status;
    }

    OperationStatus storeWakeupSchedulesToFile(const char* path)
    {
        OperationStatus status = Failed;

        if (!path)
        {
            return status;
        }

        std::vector<Schedule*>::const_iterator it;
        std::string data = "{v:1}[";

        removeExpiredSchedules();
        dumpSchedules(pastWakeupSchedules, "past");
        dumpSchedules(wakeupSchedules, "future");

        for (it = wakeupSchedules.begin(); it != wakeupSchedules.end(); it++)
        {
            data += it != wakeupSchedules.begin() ? "," : "";
            data += scheduleToString(*it);
        }

        data += "]";

        FILE* file = fopen(path, "w");

        if (!file)
        {
            ERROR_LOG("%s(): failed to open file for writing, path = '%s'", __FUNCTION__, path);
            return status;
        }

        size_t bytesWritten = fwrite(data.c_str(), 1, data.size(), file);
        fclose(file);

        if (bytesWritten == data.size())
        {
            status = Successful;
        }
        else
        {
            ERROR_LOG("%s(): failed to write file, path = '%s', bytesWritten = %zu, expected = %zu",
                __FUNCTION__, path, bytesWritten, data.size());
        }

        INFO_LOG("%s(): path = '%s', status = %d", __FUNCTION__, path, status);
        return status;
    }

  private:

    struct Schedule
    {
        Schedule(UnixTime unixTime, PowerState powerState, const char* requestorId = NULL)
        {
            requestorIds.push_back(requestorId ? requestorId : "anonymous");
            powerStates.push_back(powerState);
            this->unixTime = unixTime;
        }

        Schedule(UnixTime unixTime = 0)
        {
            this->unixTime = unixTime;
        }

        std::vector<std::string> requestorIds;
        std::vector<PowerState> powerStates;
        UnixTime unixTime;
    };

    static bool lessSchedule(Schedule* l, Schedule* r)
    {
        return (l->unixTime < r->unixTime);
    }

    std::vector<Schedule*>::iterator findUnixTime(UnixTime unixTime)
    {
        std::vector<Schedule*>::iterator result = wakeupSchedules.end();
        std::vector<Schedule*>::iterator it = wakeupSchedules.begin();

        for (; it != wakeupSchedules.end(); it++)
        {
            if ((*it)->unixTime == unixTime)
            {
                result = it;
                break;
            }
        }

        return result;
    }

    void removeExpiredSchedules()
    {
        const UnixTime currentUnixTime = (UnixTime)time(NULL);

        while (!wakeupSchedules.empty() && wakeupSchedules[0]->unixTime < currentUnixTime)
        {
            pastWakeupSchedules.push_back(*wakeupSchedules.begin());
            wakeupSchedules.erase(wakeupSchedules.begin());
        }
    }

    std::vector<std::pair<UnixTime, PowerState> > getPrioritizedSchedules(const std::vector<Schedule*>& schedules)
    {
        const PowerState priority[] = {Operational, ActiveStandby};
        std::vector<std::pair<UnixTime, PowerState> > result;

        for (std::vector<Schedule*>::const_iterator it = schedules.begin(); it != schedules.end(); it++)
        {
            for (unsigned int i = 0; i < sizeof(priority) / sizeof(priority[0]) ; i++)
            {
                std::vector<PowerState>::const_iterator maxIt = std::find(
                    (*it)->powerStates.begin(), (*it)->powerStates.end(), priority[i]);

                if (maxIt != (*it)->powerStates.end())
                {
                    result.push_back(std::make_pair((*it)->unixTime, priority[i]));
                    break;
                }
            }
        }

        return result;
    }

    std::vector<std::pair<UnixTime, PowerState> > getSchedulesByRequestorId(const std::vector<Schedule*>& schedules, const char* requestorId)
    {
        std::vector<std::pair<UnixTime, PowerState> > result;

        if (requestorId)
        {
            for (std::vector<Schedule*>::const_iterator it = schedules.begin(); it != schedules.end(); it++)
            {
                std::vector<std::string>::const_iterator rIt = (*it)->requestorIds.begin();
                std::vector<PowerState>::const_iterator pIt = (*it)->powerStates.begin();

                for (; rIt != (*it)->requestorIds.end() && pIt != (*it)->powerStates.end(); rIt++, pIt++)
                {
                    if (*rIt == requestorId)
                    {
                        result.push_back(std::make_pair((*it)->unixTime, *pIt));
                    }
                }
            }
        }

        return result;
    }

    void clearSchedules(std::vector<Schedule*>& schedules)
    {
        for (std::vector<Schedule*>::const_iterator it = schedules.begin(); it != schedules.end(); it++)
        {
            delete (*it);
        }
        schedules.clear();
    }

    void dumpSchedules(const std::vector<Schedule*>& schedules, const char* type)
    {
        for (std::vector<Schedule*>::const_iterator it = schedules.begin(); it != schedules.end(); it++)
        {
            for (size_t i = 0; i < (*it)->powerStates.size(); i++)
            {
                DEBUG_LOG("%s(): %s: unixTime = %u, powerState = '%s', requestorId = '%s' (%zu/%zu)\n",
                    __FUNCTION__, type, (*it)->unixTime, powerStateToString((*it)->powerStates[i]),
                    (*it)->requestorIds[i].c_str(), i + 1, (*it)->powerStates.size());

            }
        }
    }

    std::string scheduleToString(const Schedule *schedule)
    {
        std::vector<std::string>::const_iterator rIt = schedule->requestorIds.begin();
        std::vector<PowerState>::const_iterator pIt = schedule->powerStates.begin();
        std::ostringstream result;

        /*
            The string format of a schedule is as follows:

                {unixTime:mode1,requestorId1[|mode2,requestorId2]...}

            where:

                unixTime    - [0-9]+         (Unix Epoch time in seconds),
                mode        - [OA]           (Operational or ActiveStandby respecitvely),
                requestorId - [a-zA-Z0-9_-]+ (Requestor's unique identifier),

            with no whitespaces allowed.
        */

        result << "{" << schedule->unixTime << ":";

        for (; rIt != schedule->requestorIds.end() && pIt != schedule->powerStates.end(); rIt++, pIt++)
        {
            result << (rIt != schedule->requestorIds.begin() ? "|" : "");
            result << (*pIt == Operational ? "O" : "A") << "," << *rIt;
        }

        result << "}";
        return result.str();
    }

    bool parseUnixTime(const std::string& input, size_t& i, UnixTime& output)
    {
        /*
            Get the unix time with a simple overflow check, "[0-9]+:"...
        */
        UnixTime unixTime = 0;
        bool error = true;

        for (; i < input.size(); i++)
        {
            if (std::isdigit(input[i]))
            {
                UnixTime previousValue = unixTime;
                unixTime = unixTime * 10 + (input[i] - '0');

                if (unixTime < previousValue)
                {
                    break;
                }
            }
            else
            {
                if (input[i] == ':')
                {
                    error = false;
                    i++;
                }
                break;
            }
        }

        if (!error)
        {
            output = unixTime;
        }

        return error;
    }

    bool parsePowerState(const std::string& input, size_t& i, PowerState& output)
    {
        /*
            Get the requested power state (Operational or ActiveStandby), "[OA],"...
        */
        PowerState powerState;
        bool error = true;

        switch (input[i++])
        {
            case 'O':
            {
                if (i < input.size() && input[i] == ',')
                {
                    powerState = Operational;
                    error = false;
                    i++;
                }
                break;
            }
            case 'A':
            {
                if (i < input.size() && input[i] == ',')
                {
                    powerState = ActiveStandby;
                    error = false;
                    i++;
                }
                break;
            }
            default:
                break;
        }

        if (!error)
        {
            output = powerState;
        }

        return error;
    }

    bool parseRequestorId(const std::string& input, size_t& i, std::string& output)
    {
        /*
            Get the requestor's identifier, [a-zA-Z0-9_-]+[|}]...
        */
        std::string requestorId;
        bool error = true;

        while (i < input.size())
        {
            if (std::isalnum(input[i]) || input[i] == '-' || input[i] == '_')
            {
                requestorId += input[i];
                i++;
            }
            else if (input[i] == '|' || input[i] == '}')
            {
                if (requestorId.size())
                {
                    error = false;
                    i++;
                }
                break;
            }
            else
                break;
        }

        if (!error)
        {
            output = requestorId;
        }

        return error;
    }

    Schedule* stringToSchedule(const std::string& input)
    {
        std::vector<std::string> requestorIds;
        std::vector<PowerState> powerStates;
        OperationStatus status = Failed;
        const char* error = NULL;
        UnixTime unixTime = 0;
        Schedule *schedule = new Schedule();

        if (input.size() > 0 && input[0] == '{' && input[input.size() - 1] == '}')
        {
            size_t i = 1;

            if (!parseUnixTime(input, i, unixTime))
            {
                while (i < input.size() && input[i] != '}')
                {
                    PowerState powerState;
                    std::string requestorId;

                    if (!parsePowerState(input, i, powerState))
                    {
                        if (!parseRequestorId(input, i, requestorId))
                        {
                            requestorIds.push_back(requestorId);
                            powerStates.push_back(powerState);
                        }
                        else
                            error = "requestor id parse error";
                    }
                    else
                        error = "power state parse error";
                }
            }
            else
                error = "unix time parse error";

            if (!error && i == input.size() && unixTime && requestorIds.size())
            {
                status = Successful;
            }
        }
        else
        {
            error = "{ and/or } missing";
        }

        if (status == Successful)
        {
            schedule->requestorIds = requestorIds;
            schedule->powerStates = powerStates;
            schedule->unixTime = unixTime;
        }
        else
        {
            ERROR_LOG("%s(): malformed schedule detected! schedule = '%s', error = '%s'\n",
                __FUNCTION__, input.c_str(), error);
        }

        return schedule;
    }

    bool isAlphaNumeric(const char* input)
    {
        const char* p = input;
        bool result = true;

        for (; *p; p++)
        {
            if (!std::isalnum(*p) && (*p != '-') && (*p != '_'))
            {
                result = false;
                break;
            }
        }

        return p != input ? result : false;
    }

    const char* powerStateToString(PowerState powerState)
    {
        return powerState == Operational   ? "Operational"   :
               powerState == ActiveStandby ? "ActiveStandby" : "";
    }

    std::string vectorToString(const std::vector<std::string>& input)
    {
        std::string result;

        for (size_t i = 0; i < input.size(); i++)
        {
            result += i ? std::string(",") + input[i] : input[i];
        }

        return result;
    }

    std::vector<Schedule*> wakeupSchedules;
    std::vector<Schedule*> pastWakeupSchedules;
};

#endif
