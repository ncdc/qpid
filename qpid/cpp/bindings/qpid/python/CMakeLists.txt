#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

##------------------------------------------------------
## Use Swig to generate a literal binding to the C++ API
##------------------------------------------------------
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/python.i PROPERTIES CPLUSPLUS ON)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/python.i
   PROPERTIES SWIG_FLAGS "-I${qpid-cpp_SOURCE_DIR}/include;-I${qpid-cpp_SOURCE_DIR}/bindings")

swig_add_module(cqpid_python python ${CMAKE_CURRENT_SOURCE_DIR}/python.i)
swig_link_libraries(cqpid_python qpidmessaging qpidtypes qmf2 ${PYTHON_LIBRARIES})

set_source_files_properties(${swig_generated_file_fullname} PROPERTIES COMPILE_FLAGS "-fno-strict-aliasing")
include_directories(${PYTHON_INCLUDE_PATH}
                    ${qpid-cpp_SOURCE_DIR}/include
                    ${qpid-cpp_SOURCE_DIR}/bindings)

##------------------------------------
## Install the complete Python binding
##------------------------------------
install(CODE "execute_process(COMMAND ${PYTHON_EXECUTABLE} -m py_compile cqpid.py
                              WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})")
install(CODE "execute_process(COMMAND ${PYTHON_EXECUTABLE} -O -m py_compile cqpid.py
                              WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cqpid.py
              ${CMAKE_CURRENT_BINARY_DIR}/cqpid.pyc
              ${CMAKE_CURRENT_BINARY_DIR}/cqpid.pyo
        DESTINATION ${PYTHON_SITEARCH_PACKAGES}
        COMPONENT ${QPID_COMPONENT_CLIENT}
        )
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/_cqpid_python.so
        RENAME _cqpid.so
        DESTINATION ${PYTHON_SITEARCH_PACKAGES}
        COMPONENT ${QPID_COMPONENT_CLIENT}
        )

