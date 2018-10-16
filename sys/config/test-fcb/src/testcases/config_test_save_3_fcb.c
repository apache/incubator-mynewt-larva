/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include "conf_test_fcb.h"

TEST_CASE(config_test_save_3_fcb)
{
    int rc;
    struct conf_fcb cf;
    int i;

    config_wipe_srcs();
    config_wipe_fcb(fcb_range, fcb_range[0].sr_sector_count);

    cf.cf_fcb.f_magic = MYNEWT_VAL(CONFIG_FCB_MAGIC);
    cf.cf_fcb.f_ranges = fcb_range;
    cf.cf_fcb.f_range_cnt = 1;
    cf.cf_fcb.f_sector_cnt = 4;

    rc = conf_fcb_src(&cf);
    TEST_ASSERT(rc == 0);

    rc = conf_fcb_dst(&cf);
    TEST_ASSERT(rc == 0);

    for (i = 0; i < 4096; i++) {
        val32 = i;

        rc = conf_save();
        TEST_ASSERT(rc == 0);

        val32 = 0;

        rc = conf_load();
        TEST_ASSERT(rc == 0);
        TEST_ASSERT(val32 == i);
    }
}
