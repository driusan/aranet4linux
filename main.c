#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <dbus/dbus.h>


typedef struct {
    char *name;

    char *devicename;
    char *address;
    char *objectpath;


    int paired;
    int connected;

    char *gattobjectpath;
} aranet4_device;

typedef struct {
    u_int16_t co2;
    float temperature;
    float airpressure;
    u_int8_t humidity;
    u_int8_t battery;
} sensor_reading;

void connectBLE(DBusConnection* conn, aranet4_device *dev);
void readSensorData(DBusConnection* conn, aranet4_device *dev, sensor_reading *dst);

static void getVariantString(DBusMessageIter *iter, char **val) {
    DBusMessageIter valueiter;
    dbus_message_iter_recurse(iter, &valueiter);
    dbus_message_iter_get_basic(&valueiter, val);

}

static void getVariantBool(DBusMessageIter *iter, int *val) {
    DBusMessageIter valueiter;
    dbus_message_iter_recurse(iter, &valueiter);
    dbus_message_iter_get_basic(&valueiter, val);

}



void connectBLE(DBusConnection* conn, aranet4_device *dev) {
    DBusMessage* msg = NULL;
    DBusMessage* reply = NULL;
    DBusMessageIter args;
    DBusError err;

    fprintf(stderr, "Connecting..\n");
    msg = dbus_message_new_method_call("org.bluez", 
            dev->objectpath,
            dev->devicename,
            "Connect");
    if (NULL == msg) {
        fprintf(stderr, "Message Null\n");
        exit(1);
    }
    dbus_message_iter_init_append(msg, &args);

    // send connect message
    dbus_error_init(&err);
    reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "Connection Error (%s)\n", err.message);
        dbus_error_free(&err);
    }
    if (reply == 0) {
        fprintf(stderr, "No reply\n");
        exit(1);
    }
    dbus_connection_flush(conn);

    // free message
   dbus_message_unref(msg);
}

static void append_dictentry(DBusMessageIter *dictentry) {
    DBusMessageIter array, entry, variant;
    char *offsetkey = "offset";
    int offset = 0;
    // FIXME: Use DBUS constants instead of hardcoding {sv}
    if (!dbus_message_iter_open_container(dictentry, DBUS_TYPE_ARRAY,
    "{sv}", &array)) {
        fprintf(stderr, "no array");
        exit(1);
    }
    if (!dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &entry)) {
        fprintf(stderr, "no open container");
        exit(1);
    }

    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &offsetkey);

    if (!dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, DBUS_TYPE_UINT16_AS_STRING, &variant)) {
        fprintf(stderr, "no open container");
        exit(1);
    }

    dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT16, &offset);

    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(&array, &entry);
    dbus_message_iter_close_container(dictentry, &array);

}

void readSensorData(DBusConnection* conn, aranet4_device *dev, sensor_reading *dst) {
    DBusMessage* msg = NULL;
    DBusMessageIter args;
    DBusMessageIter dictentry;
    DBusPendingCall *pending = NULL;

    msg = dbus_message_new_method_call("org.bluez",
            dev->gattobjectpath,
            "org.bluez.GattCharacteristic1",
            "ReadValue");
    if (NULL == msg) {
        fprintf(stderr, "Message Null\n");
        exit(1);
    }

    dbus_message_iter_init_append(msg, &dictentry);

    append_dictentry(&dictentry);

    // send message and get a handle for a reply
    if (!dbus_connection_send_with_reply(conn, msg, &pending, -1)) {
        fprintf(stderr, "Out of memory!\n");
        exit(1);
    }
    if (pending == NULL) {
        fprintf(stderr, "null pending\n");
        exit(1);

    }
    dbus_connection_flush(conn);
    dbus_message_unref(msg);

    dbus_pending_call_block(pending);
    msg = dbus_pending_call_steal_reply(pending);
    if (msg == NULL) {
        fprintf(stderr, "null reply");
        exit(1);
    }
    dbus_pending_call_unref(pending);

    // Get the response
    if (!dbus_message_iter_init(msg, &args)) {
        fprintf(stderr, "no reply data\n");
    }
    int nitems = dbus_message_iter_get_element_count(&args);

    DBusMessageIter elements;
    dbus_message_iter_recurse(&args, &elements);

    u_char* value;
    value = malloc(sizeof(char)*nitems);
    int i = 0;
    do {
        if (dbus_message_iter_get_arg_type(&elements) != DBUS_TYPE_BYTE) {
            fprintf(stderr, "invalid value type\n");
            continue;
        }
        dbus_message_iter_get_basic(&elements, &value[i++]);
    } while (dbus_message_iter_next(&elements)); 

    // Values as interpreted from:
    // https://github.com/Anrijs/Aranet4-Python/blob/master/docs/UUIDs.md
    // Cast anything divided to a float to ensure it gets rounded correctly,
    // even if we never display the decimal.
    dst->co2 = value[1] << 8 | value[0];
    dst->temperature = (value[3] << 8 | value[2]) / 20.0f;
    dst->airpressure = (value[5] << 8 | value[4]) / 10.0f;
    // Percentages, 0-100
    dst->humidity = value[6];
    dst->battery = value[7];
}

static void load_device(DBusConnection* conn, aranet4_device* dev);
int main(void)
{
    DBusError err;
    DBusConnection* conn;
    sensor_reading data;
    aranet4_device dev;

    dev.name = NULL;
    dev.devicename = NULL;
    dev.objectpath = NULL;
    dev.gattobjectpath = NULL;
    dev.paired = 0;
    dev.connected = 0;

    // initialise the errors
    dbus_error_init(&err);

    // connect to the bus
    conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "Connection Error (%s)\n", err.message);
        dbus_error_free(&err);
    }
    if (NULL == conn) {
        exit(1);
    }

    if(!getenv("ARANET4_ADDRESS")) {
        fprintf(stderr, "Environment variable ARANET4_ADDRESS must be set to bluetooth address");
        exit(1);
    }
    dev.address = getenv("ARANET4_ADDRESS");

    load_device(conn, &dev);

    if(dev.devicename == NULL) {
        fprintf(stderr, "Could not find device %s\n", dev.address);
        exit(1);
    }

    if(!dev.paired) {
        fprintf(stderr, "%s not paired\n", dev.address);
        exit(1);
    }

    // Connect to device over BLE
    if(!dev.connected) 
        connectBLE(conn, &dev);

    if (dev.gattobjectpath == NULL) {
        fprintf(stderr, "Could not find GattCharacteristic to read sensor data\n");
        exit(1);
    }
    readSensorData(conn, &dev, &data);

    // Dump all the values read
    printf("%s\nCO2: %d\nTemperature: %.1fC\nPressure: %.0fhPa\nHumidity: %d%%\nBattery: %d%%", dev.name, data.co2, data.temperature, data.airpressure, data.humidity, data.battery);

    dbus_connection_unref(conn);

    exit(0);
}

static void inspect_object(DBusMessageIter *entry, aranet4_device *dev) {
    DBusMessageIter dictentry;
    DBusMessageIter arrayiter;
    DBusMessageIter arraydictiter;
    DBusMessageIter subarrayiter;
    DBusMessageIter subarraydictiter;
    char *objectpath, *propname;

    // Read name
    dbus_message_iter_recurse(entry, &dictentry);
    // dbus_message_iter_next(&dictentry); 
    if (dbus_message_iter_get_arg_type(&dictentry) != DBUS_TYPE_OBJECT_PATH) {
        fprintf(stderr, "Invalid entry name\n");
        exit(1);
    };
    dbus_message_iter_get_basic(&dictentry, &objectpath);

    // Read value
    dbus_message_iter_next(&dictentry); 
    dbus_message_iter_recurse(&dictentry, &arrayiter);

    // Go into the array of dict entries for the path
    do {
        if (dbus_message_iter_get_arg_type(&arrayiter) != DBUS_TYPE_DICT_ENTRY) {
            continue;
        }
        dbus_message_iter_recurse(&arrayiter, &arraydictiter);
        dbus_message_iter_get_basic(&arraydictiter, &propname);
        dbus_message_iter_next(&arraydictiter); 

        // found means we found the bt address in this objectpath,
        // so we keep looking at properties to populate the rest of
        // the device
        int found = 0;
        int nitems = dbus_message_iter_get_element_count(&arraydictiter);
        if (nitems > 0) {
            // Go through the dictionary of properties for each objectpath
            dbus_message_iter_recurse(&arraydictiter, &subarrayiter);

            char *subpropname;
            do {
                if (dbus_message_iter_get_arg_type(&arrayiter) != DBUS_TYPE_DICT_ENTRY) {
                    continue;
                }
                dbus_message_iter_recurse(&subarrayiter, &subarraydictiter);
                do {
                    dbus_message_iter_get_basic(&subarraydictiter, &subpropname);
                    dbus_message_iter_next(&subarraydictiter); 

                    // This objectpath has an Address, does it match the one
                    // of the device we're looking for?
                    if (strcmp(subpropname, "Address") == 0) {
                        char *val;
                        getVariantString(&subarraydictiter, &val);
                        if (strcmp(val, dev->address) == 0) {
                            dev->objectpath = objectpath;
                            dev->devicename = propname;
                            found = 1;
                        }
                    } else if (found && strcmp(subpropname, "Name") == 0) {
                        getVariantString(&subarraydictiter, &dev->name);
                    } else if (found && strcmp(subpropname, "Paired") == 0) {
                        getVariantBool(&subarraydictiter, &dev->paired);
                    } else if (found && strcmp(subpropname, "Connected") == 0) {
                        getVariantBool(&subarraydictiter, &dev->connected);
                    }

                    // This objectpath has a UUID, check if it's the object path
                    // for the GattCharacteristic descriptor we want with the current
                    // sensor data.
                    // UUID from 
                    // https://github.com/Anrijs/Aranet4-Python/blob/master/docs/UUIDs.md
                    if (strcmp(subpropname, "UUID") == 0) {
                        char *val;
                        getVariantString(&subarraydictiter, &val);
                        if(strcmp(val, "f0cd1503-95da-4f4b-9ac8-aa55d312af0c") == 0) {
                            dev->gattobjectpath = objectpath;
                        }
                    }


                } while (dbus_message_iter_next(&subarraydictiter)); 
            } while (dbus_message_iter_next(&subarrayiter)); 
        }

    } while (dbus_message_iter_next(&arrayiter)); 
}

static void load_device(DBusConnection* conn, aranet4_device* dev) {
    DBusMessage* msg = NULL;
    DBusMessage* reply = NULL;
    DBusMessageIter args;
    DBusError err;
    msg = dbus_message_new_method_call("org.bluez", 
            "/",
            "org.freedesktop.DBus.ObjectManager",
            "GetManagedObjects");
    dbus_message_iter_init_append(msg, &args);

    dbus_error_init(&err);
    reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "Connection Error (%s)\n", err.message);
        dbus_error_free(&err);
    }
    if (reply == 0) {
        fprintf(stderr, "No reply\n");
        exit(1);
    }
    dbus_connection_flush(conn);

   // read the parameters
   DBusMessageIter objects; 
   if (!dbus_message_iter_init(reply, &args))
      fprintf(stderr, "Message has no arguments!\n");
   else if (DBUS_TYPE_ARRAY != dbus_message_iter_get_arg_type(&args))
      fprintf(stderr, "Argument is not array!\n");
   else {
      dbus_message_iter_recurse(&args, &objects);
      do {
          if (dbus_message_iter_get_arg_type(&objects) != DBUS_TYPE_DICT_ENTRY) {
              continue;
          }
          inspect_object(&objects, dev);
          //printDictEntry(&props);

      } while (dbus_message_iter_next(&objects)); 
   }

   // free reply and close connection
   dbus_message_unref(msg);
}
