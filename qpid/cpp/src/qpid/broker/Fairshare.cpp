/*
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */
#include "qpid/broker/Fairshare.h"
#include "qpid/broker/QueuedMessage.h"
#include "qpid/framing/FieldTable.h"
#include "qpid/log/Statement.h"
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>

namespace qpid {
namespace broker {

Fairshare::Fairshare(size_t levels, uint32_t limit) :
    PriorityQueue(levels),
    limits(levels, limit), priority(levels-1), count(0) {}


void Fairshare::setLimit(size_t level, uint32_t limit)
{
    limits[level] = limit;
}

bool Fairshare::limitReached()
{
    uint32_t l = limits[priority];
    return l && ++count > l;
}

uint8_t Fairshare::currentLevel()
{
    if (limitReached()) {
        return nextLevel();
    } else {
        return priority;
    }
}

uint8_t Fairshare::nextLevel()
{
    count = 1;
    if (priority) --priority;
    else priority = levels-1;
    return priority;
}

bool Fairshare::isNull()
{
    for (int i = 0; i < levels; i++) if (limits[i]) return false;
    return true;
}

bool Fairshare::getState(uint8_t& p, uint32_t& c) const
{
    p = priority;
    c = count;
    return true;
}

bool Fairshare::setState(uint8_t p, uint32_t c)
{
    priority = p;
    count = c;
    return true;
}

bool Fairshare::findFrontLevel(uint8_t& p, PriorityLevels& messages)
{
    const uint8_t start = p = currentLevel();
    do {
        if (!messages[p].empty()) return true;
    } while ((p = nextLevel()) != start);
    return false;
}



bool Fairshare::getState(const Messages& m, uint8_t& priority, uint32_t& count)
{
    const Fairshare* fairshare = dynamic_cast<const Fairshare*>(&m);
    return fairshare && fairshare->getState(priority, count);
}

bool Fairshare::setState(Messages& m, uint8_t priority, uint32_t count)
{
    Fairshare* fairshare = dynamic_cast<Fairshare*>(&m);
    return fairshare && fairshare->setState(priority, count);
}

int getIntegerSetting(const qpid::framing::FieldTable& settings, const std::vector<std::string>& keys)
{
    qpid::framing::FieldTable::ValuePtr v;
    std::vector<std::string>::const_iterator i = keys.begin(); 
    while (!v && i != keys.end()) {
        v = settings.get(*i++);
    }

    if (!v) {
        return 0;
    } else if (v->convertsTo<int>()) {
        return v->get<int>();
    } else if (v->convertsTo<std::string>()){
        std::string s = v->get<std::string>();
        try {
            return boost::lexical_cast<int>(s);
        } catch(const boost::bad_lexical_cast&) {
            QPID_LOG(warning, "Ignoring invalid integer value for " << *i << ": " << s);
            return 0;
        }
    } else {
        QPID_LOG(warning, "Ignoring invalid integer value for " << *i << ": " << *v);
        return 0;
    }
}

int getIntegerSettingForKey(const qpid::framing::FieldTable& settings, const std::string& key)
{
    return getIntegerSetting(settings, boost::assign::list_of<std::string>(key));
}

int getSetting(const qpid::framing::FieldTable& settings, const std::vector<std::string>& keys, int minvalue, int maxvalue)
{
    return std::max(minvalue,std::min(getIntegerSetting(settings, keys), maxvalue));
}

std::auto_ptr<Fairshare> getFairshareForKey(const qpid::framing::FieldTable& settings, uint8_t levels, const std::string& key)
{
    uint8_t defaultLimit = getIntegerSettingForKey(settings, key);
    std::auto_ptr<Fairshare> fairshare(new Fairshare(levels, defaultLimit));
    for (uint8_t i = 0; i < levels; i++) {
        std::string levelKey = (boost::format("%1%-%2%") % key % i).str();
        if(settings.isSet(levelKey)) {
            fairshare->setLimit(i, getIntegerSettingForKey(settings, levelKey));
        }
    }
    if (!fairshare->isNull()) {
        return fairshare;
    } else {
        return std::auto_ptr<Fairshare>();
    }
}

std::auto_ptr<Fairshare> getFairshare(const qpid::framing::FieldTable& settings,
                                      uint8_t levels,
                                      const std::vector<std::string>& keys)
{
    std::auto_ptr<Fairshare> fairshare;
    for (std::vector<std::string>::const_iterator i = keys.begin(); i != keys.end() && !fairshare.get(); ++i) {
        fairshare = getFairshareForKey(settings, levels, *i);
    }
    return fairshare;
}

std::auto_ptr<Messages> Fairshare::create(const qpid::framing::FieldTable& settings)
{
    using boost::assign::list_of;
    std::auto_ptr<Messages> result;
    size_t levels = getSetting(settings, list_of<std::string>("qpid.priorities")("x-qpid-priorities"), 0, 100);
    if (levels) {
        std::auto_ptr<Fairshare> fairshare =
            getFairshare(settings, levels, list_of<std::string>("qpid.fairshare")("x-qpid-fairshare"));
        if (fairshare.get()) result = fairshare;
        else result = std::auto_ptr<Messages>(new PriorityQueue(levels));
    }
    return result;
}

}} // namespace qpid::broker
