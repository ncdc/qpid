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

#include <exception>
#include <iostream>
#include <stdlib.h>
#include <time.h>
#include "qpid/Options.h"

namespace qpid {
namespace tests {

struct Args : public qpid::Options
{
    uint32_t count;
    uint32_t minSize;
    uint32_t maxSize;
    uint32_t minChar;
    uint32_t maxChar;
    bool help;

    Args() : qpid::Options("Random data generator"),
             count(1), minSize(8), maxSize(4096),
             minChar(32), maxChar(126),//safely printable ascii chars
             help(false)
    {
        addOptions()
            ("count", qpid::optValue(count, "N"), "number of data strings to generate")
            ("min-size", qpid::optValue(minSize, "N"), "minimum size of data string")
            ("max-size", qpid::optValue(maxSize, "N"), "maximum size of data string")
            ("min-char", qpid::optValue(minChar, "N"), "minimum char value used in data string")
            ("max-char", qpid::optValue(maxChar, "N"), "maximum char value used in data string")
            ("help", qpid::optValue(help), "print this usage statement");
    }

    bool parse(int argc, char** argv) {
        try {
            qpid::Options::parse(argc, argv);
            if (maxSize < minSize) throw qpid::Options::Exception("max-size must be greater than min-size");
            if (maxChar < minChar) throw qpid::Options::Exception("max-char must be greater than min-char");

            if (help) {
                std::cerr << *this << std::endl << std::endl;
            } else {
                return true;
            }
        } catch (const std::exception& e) {
            std::cerr << *this << std::endl << std::endl << e.what() << std::endl;
        }
        return false;
    }

};

uint32_t random(uint32_t min, uint32_t max)
{
    return (rand() % (max-min+1)) + min;
}

std::string generateData(uint32_t size, uint32_t min, uint32_t max)
{
    std::string data;
    for (uint32_t i = 0; i < size; i++) {
        data += (char) random(min, max);
    }
    return data;
}

}} // namespace qpid::tests

using namespace qpid::tests;

int main(int argc, char** argv)
{
    Args opts;
    if (opts.parse(argc, argv)) {
        srand(time(0));
        for (uint32_t i = 0; i < opts.count; i++) {
            std::cout << generateData(random(opts.minSize, opts.maxSize), opts.minChar, opts.maxChar) << std::endl;
        }
        return 0;
    } else {
        return 1;
    }
}
