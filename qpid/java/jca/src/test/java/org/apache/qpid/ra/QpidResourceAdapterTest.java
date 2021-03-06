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
package org.apache.qpid.ra;

import javax.resource.spi.ResourceAdapterInternalException;

import java.io.ByteArrayOutputStream;
import java.io.ObjectOutputStream;

import junit.framework.TestCase;


public class QpidResourceAdapterTest extends TestCase
{
    public void testGetXAResources() throws Exception
    {
        QpidResourceAdapter ra = new QpidResourceAdapter();
        assertNull(ra.getXAResources(null));
    }

    public void testTransactionManagerLocatorException() throws Exception
    {

        QpidResourceAdapter ra = new QpidResourceAdapter();
        assertNull(ra.getTransactionManagerLocatorClass());
        assertNull(ra.getTransactionManagerLocatorMethod());

        try
        {
            ra.start(null);
        }
        catch(ResourceAdapterInternalException e)
        {

        }

        ra.setTransactionManagerLocatorClass("DummyLocator");

        try
        {
            ra.start(null);
        }
        catch(ResourceAdapterInternalException e)
        {

        }

    }

    public void testResourceAdapterBasicSerialization() throws Exception
    {

        QpidResourceAdapter ra = new QpidResourceAdapter();
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        ObjectOutputStream oos = new ObjectOutputStream(out);
        oos.writeObject(ra);
        oos.close();
        assertTrue(out.toByteArray().length > 0);
    }
}
