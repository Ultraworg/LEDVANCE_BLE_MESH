#ifndef LAMP_NVS_H
#define LAMP_NVS_H

#include "esp_err.h"

typedef struct {
    char name[50];
    char address[8];
} LampInfo;

// Function to save lamp information to NVS
esp_err_t save_lamp_info(LampInfo *lamp_info, int index);
// Function to load lamp information from NVS
esp_err_t load_lamp_info(LampInfo *lamp_info, int index);
// Function to remove a lamp from NVS by index
esp_err_t remove_lamp_info(int index);
// Function to find the next free index in the NVS store
int findNextFreeIndexInNVS();
// Function to find the index of a lamp by its name or address
int find_index_by_name_or_address(const char *name, const char *address);
void printAllLampInfo();
// Function to get the current number of lamps
int getCurrentNumberOfLamps();

#endif /* LAMP_NVS_H */
