  /**
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

#include <assert.h>
#include <string.h>
#include "testutil/testutil.h"
#include "test_json.h"
#include "json/json.h"

static char *output = "{\"KeyBool\": true,\"KeyInt\": -1234,\"KeyUint\": 1353214,\"KeyString\": \"foobar\",\"KeyStringN\": \"foobarlong\",\"KeyIntArr\": [153,2532,-322]}";
static char *output2 = "{\"KeySructArr\" : [{true, 2003, 'foobar'},{false, 2035, 'foobarst'}]}";
static char bigbuf[512];
static int buf_index;

static int test_write(void *buf, char* data, int len) {
    int i;
    for(i = 0; i < len; i++) {
        bigbuf[buf_index++] = data[i];        
    }
    return len;
}

TEST_CASE(test_json_simple_encode){
    struct json_encoder encoder;
    struct json_value value;
    int rc;
    
    /* reset the state of the internal test */
    buf_index = 0;    
    memset(&encoder, 0, sizeof(encoder));
    
    encoder.je_write = test_write;
    encoder.je_arg= NULL;
    
    rc = json_encode_object_start(&encoder);    
    TEST_ASSERT(rc == 0);
    
    JSON_VALUE_BOOL(&value, 1);
    rc = json_encode_object_entry(&encoder, "KeyBool", &value);
    TEST_ASSERT(rc == 0);
    
    JSON_VALUE_INT(&value, -1234);
    rc = json_encode_object_entry(&encoder, "KeyInt", &value);
    TEST_ASSERT(rc == 0);    

    JSON_VALUE_UINT(&value, 1353214);
    rc = json_encode_object_entry(&encoder, "KeyUint", &value);
    TEST_ASSERT(rc == 0);    
    
    JSON_VALUE_STRING(&value, "foobar");
    rc = json_encode_object_entry(&encoder, "KeyString", &value);
    TEST_ASSERT(rc == 0);    
    
    /* we'll decode later differently */
    JSON_VALUE_STRINGN(&value, "foobarlongstring", 10);
    rc = json_encode_object_entry(&encoder, "KeyStringN", &value);
    TEST_ASSERT(rc == 0);  

    rc = json_encode_array_name(&encoder, "KeyIntArr");
    TEST_ASSERT(rc == 0);

    rc = json_encode_array_start(&encoder);
    TEST_ASSERT(rc == 0);

    JSON_VALUE_INT(&value, 153);
    rc = json_encode_array_value(&encoder, &value);
    TEST_ASSERT(rc == 0);

    JSON_VALUE_INT(&value, 2532);
    rc = json_encode_array_value(&encoder, &value);
    TEST_ASSERT(rc == 0);

    JSON_VALUE_INT(&value, -322);
    rc = json_encode_array_value(&encoder, &value);
    TEST_ASSERT(rc == 0);

    rc = json_encode_array_finish(&encoder);
    TEST_ASSERT(rc == 0);

    rc = json_encode_object_finish(&encoder);    
    TEST_ASSERT(rc == 0);
    
    /* does it match what we expect it to */
    rc = strcmp(bigbuf, output);
    TEST_ASSERT(rc == 0);
}


/* a test structure to hold the json flat buffer and pass bytes
 * to the decoder */
struct test_jbuf {
    /* json_buffer must be first element in the structure */
    struct json_buffer json_buf;
    char * start_buf;
    char * end_buf;
    int current_position;
};


static char 
test_jbuf_read_next(struct json_buffer *jb) {
    char c;
    struct test_jbuf  *ptjb = (struct test_jbuf*) jb;    
    
    if((ptjb->start_buf + ptjb->current_position) <= ptjb->end_buf) {
        c = *(ptjb->start_buf + ptjb->current_position);
        ptjb->current_position++;
        return c;
    }
    return '\0';    
}

/* this goes backward in the buffer one character */
static char 
test_jbuf_read_prev(struct json_buffer *jb) {
    char c;
    struct test_jbuf  *ptjb = (struct test_jbuf*) jb;
    if(ptjb->current_position) {
       ptjb->current_position--; 
       c = *(ptjb->start_buf + ptjb->current_position);
       return c;
    }
    
    /* can't rewind */
    return '\0';
    
}

static int 
test_jbuf_readn(struct json_buffer *jb, char *buf, int size) {
    struct test_jbuf  *ptjb = (struct test_jbuf*) jb;    

    int remlen;

    remlen = ptjb->end_buf - (ptjb->start_buf + ptjb->current_position);
    if (size > remlen) {
        size = remlen;
    }

    memcpy(buf, ptjb->start_buf + ptjb->current_position, size);
    ptjb->current_position += size;
    return size;
}

static void 
test_buf_init(struct test_jbuf *ptjb, char *string) {
    /* initialize the decode */
    ptjb->json_buf.jb_read_next = test_jbuf_read_next;
    ptjb->json_buf.jb_read_prev = test_jbuf_read_prev;
    ptjb->json_buf.jb_readn = test_jbuf_readn;
    ptjb->start_buf = string;
    ptjb->end_buf = string + strlen(string);  
    /* end buf points to the NULL */
    ptjb->current_position = 0;
}

/* now test the decode on a string */
TEST_CASE(test_json_simple_decode){
    struct test_jbuf tjb; 
    long long unsigned int uint_val;
    long long int int_val;
    bool bool_val;
    char string1[16];
    char string2[16];
    long long int intarr[8];
    int rc;
    int array_count;
    
    /*working with structures of arrrays. declearing variable*/
    
    struct test_structarr {
        
        bool bool_val2;
        long long int int_val2;
        char stringstruct2 [16];
        
    };
    
    struct test_structarr studarr[3];
     struct test_jbuf tjb2; 
    int array_count2;
    int rc2;
    char base;
    

    struct json_attr_t test_attr[7] = {
        [0] = {
            .attribute = "KeyBool",
            .type = t_boolean,
            .addr.boolean = &bool_val,
            .nodefault = true
        },
        [1] = {
            .attribute = "KeyInt",
            .type = t_integer,
            .addr.integer = &int_val,
            .nodefault = true
            },
        [2] = {
            .attribute = "KeyUint",
            .type = t_uinteger,
            .addr.uinteger = &uint_val,
            .nodefault = true
            },
        [3] = {
            .attribute = "KeyString",
            .type = t_string,
            .addr.string = string1,
            .nodefault = true,
            .len = sizeof(string1)
            },
        [4] = {
            .attribute = "KeyStringN",
            .type = t_string,
            .addr.string = string2,
            .nodefault = true,
            .len = sizeof(string2)
        },
        [5] = {
            .attribute = "KeyIntArr",
            .type = t_array,
            .addr.array = {
                .element_type = t_integer,
                .arr.integers.store = intarr,
                .maxlen = sizeof intarr / sizeof intarr[0],
                .count = &array_count,
            },
            .nodefault = true,
            .len = sizeof(intarr)
        },
        [6] = {
            .attribute = NULL
        }
    };

    test_buf_init(&tjb, output);

    rc = json_read_object(&tjb.json_buf, test_attr);
    TEST_ASSERT(rc==0);
    TEST_ASSERT(bool_val == 1);
    TEST_ASSERT(int_val ==  -1234);
    TEST_ASSERT(uint_val == 1353214);

    rc = memcmp(string1, "foobar", strlen("foobar"));
    TEST_ASSERT(rc==0);

    rc = memcmp(string2, "foobarlongstring", 10);
    TEST_ASSERT(rc==0);

    TEST_ASSERT(array_count == 3);
    TEST_ASSERT(intarr[0] == 153);
    TEST_ASSERT(intarr[1] == 2532);
    TEST_ASSERT(intarr[2] == -322);
    
    /*working with struct of arrays . declearing sub_arr*/
    struct json_attr_t sub_test_attr2[3] = {
       [0] = {
            .attribute = "KeyBool",
            .type = t_boolean,
            .addr.boolean = &studarr->bool_val2,
            .nodefault = true
       },
       
        [1] = {
            .attribute = "KeyInt",
            .type = t_integer,
            .addr.integer = &studarr->int_val2,
            .nodefault = true
        },
        
        [2] = {
            .attribute = "KeyString",
            .type = t_string,
            .addr.string = studarr->stringstruct2,
            .nodefault = true,
            .len = sizeof(studarr->stringstruct2)
        },
     };
    
    /*second attr to initiaze the struct*/
     struct json_attr_t test_attr2[1] = {
        [0] = {
             .attribute = "KeyStructArr",
            .type = t_array,
            .addr.array = {
                .element_type = t_structobject,
                .arr.objects.subtype = sub_test_attr2,
                .arr.objects.base = &base,
                .arr.objects.stride = sizeof(studarr),
                .maxlen = sizeof studarr / sizeof studarr[0],
                .count = &array_count2,
            },
            .nodefault = true,
            .len = sizeof(studarr)
        },
    };  
    
    
    test_buf_init(&tjb2, output2);
    rc2 = json_read_object(&tjb2.json_buf, test_attr2);
    TEST_ASSERT(rc2==0);
    TEST_ASSERT(studarr[0].bool_val2 == true);
    TEST_ASSERT(studarr[0].int_val2 ==  2003);
    rc2 = memcmp(studarr[0].stringstruct2 ,"foobar", strlen("foobar"));
    TEST_ASSERT(rc2 == 0);
    
    TEST_ASSERT(studarr[1].bool_val2 == false);
    TEST_ASSERT(studarr[1].int_val2 ==  2035);
    rc2 = memcmp(studarr[1].stringstruct2 ,"foobarst", strlen("foobarst"));
    TEST_ASSERT(rc2 == 0);
    
    
}
