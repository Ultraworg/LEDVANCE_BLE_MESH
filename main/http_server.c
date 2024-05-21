#include "http_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>
#include "cJSON.h"

#include "lamp_nvs.h"

#define TAG "HTTP_SERVER"
// Define the maximum number of lamps
#define MAX_LAMPS 20

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* An HTTP POST handler */
esp_err_t add_lamp_post_handler(httpd_req_t *req)
{
    char content[100];
    int ret, remaining = req->content_len;

    if (remaining > sizeof(content)) {
        /* Respond with 413 (Request Entity Too Large) */
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "Content too long");
        /* Return to close the connection */
        return ESP_OK;
    }

    while (remaining > 0) {
        /* Read the data for the request */
        if ((ret = httpd_req_recv(req, content, MIN(remaining, sizeof(content)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                /* Retry receiving if timeout occurred */
                continue;
            }
            /* In case of unrecoverable error, respond with 500 (Internal Server Error) */
            httpd_resp_send_500(req);
            /* Return to close the connection */
            return ESP_FAIL;
        }
        remaining -= ret;

        // Parse received data
        char lamp_name[50];
        char lamp_address_str[8];
        ESP_LOGI(TAG, "content %s", content);
        sscanf(content, "lamp_name=%[^&]&lamp_address=%6s", lamp_name, lamp_address_str);
        //int lamp_address = strtol(lamp_address_str, NULL, 16);
        ESP_LOGI(TAG, "lamp adress %s", lamp_address_str);
        ESP_LOGI(TAG, "lamp name %s", lamp_name);

        int nextFreeNVSIndex = findNextFreeIndexInNVS();
        // Add your logic here to add the new lamp

        LampInfo lamp;
        strcpy(lamp.name, lamp_name);
        strcpy(lamp.address, lamp_address_str);
        // Save lamp info
        esp_err_t err = save_lamp_info(&lamp, nextFreeNVSIndex);
        if (err != ESP_OK) {
            printf("Failed to save lamp info: %d\n", err);
        }

        // Load all lamps info
        printAllLampInfo();

    }

    // After the lamp is successfully added, send a response with a JavaScript redirect
    const char* resp_str =
        "<html><head>"
        "<script>window.location.replace('/');</script>"
        "</head></html>";

    httpd_resp_send(req, resp_str, strlen(resp_str));
    return ESP_OK;
}

// HTTP POST handler for removing lamps
esp_err_t remove_lamp_post_handler(httpd_req_t *req)
{
    char content[100];
    int ret, remaining = req->content_len;

    if (remaining > sizeof(content)) {
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "Content too long");
        return ESP_OK;
    }

    while (remaining > 0) {
        if ((ret = httpd_req_recv(req, content, MIN(remaining, sizeof(content)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        remaining -= ret;

        // Parse received data to get lamp name or address to remove
        char lamp_name[50];
        char lamp_address_str[8];
        sscanf(content, "lamp_name=%[^&]&lamp_address=%6s", lamp_name, lamp_address_str);

        // Add your logic here to remove the lamp
        ESP_LOGI(TAG, "lamp adress %s", lamp_address_str);
        ESP_LOGI(TAG, "lamp name %s", lamp_name);
        // For example:
        int index_to_remove = find_index_by_name_or_address(lamp_name, lamp_address_str);
        if (index_to_remove >= 0) {
            esp_err_t err = remove_lamp_info(index_to_remove);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Lamp removed successfully");
                // After the lamp is successfully added, send a response with a JavaScript redirect
                const char* resp_str =
                    "<html><head>"
                    "<script>window.location.replace('/');</script>"
                    "</head></html>";

                httpd_resp_send(req, resp_str, strlen(resp_str));
            } else {
                ESP_LOGE(TAG, "Failed to remove lamp: %d", err);
            }
        } else {
            ESP_LOGE(TAG, "Lamp not found for removal");
            httpd_resp_sendstr(req, "No Lamp found with this name or address");
        }
    }

    return ESP_OK;
}

// HTTP GET handler to retrieve all lamp information
esp_err_t get_lamps_handler(httpd_req_t *req) {
    // Retrieve all lamp information from NVS
    cJSON *root = cJSON_CreateArray();

    // Iterate through all lamps
    for (int i = 0; i < MAX_LAMPS; i++) {
        LampInfo lamp_info;
        esp_err_t err = load_lamp_info(&lamp_info, i);
        if (err == ESP_OK) {
            // Add lamp info to JSON array
            cJSON *lamp_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(lamp_obj, "name", lamp_info.name);
            cJSON_AddStringToObject(lamp_obj, "address", lamp_info.address);
            cJSON_AddItemToArray(root, lamp_obj);        
        }
    }

    // Convert cJSON object to a string
    char *json_str = cJSON_Print(root);

    // Send JSON string as the response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));

    // Free cJSON object and JSON string
    cJSON_Delete(root);
    free(json_str);

    return ESP_OK;
}

// HTTP GET handler to serve the HTML page with the button
esp_err_t add_lamp_get_handler(httpd_req_t *req)
{
    const char* resp_str = 
        "<html><body>"
        "<h1>Add New Lamp</h1>"
        "<form action=\"/add_lamp\" method=\"post\">"
        "<label for=\"lamp_name\">Lamp Name:</label>"
        "<input type=\"text\" id=\"lamp_name\" name=\"lamp_name\"><br><br>"
        "<label for=\"lamp_address\">Lamp Address:</label>"
        "<input type=\"text\" id=\"lamp_address\" name=\"lamp_address\"><br><br>"
        "<input type=\"submit\" value=\"Add Lamp\">"
        "</form>"
        "<br><form action=\"/\" method=\"get\"><input type=\"submit\" value=\"Back\"></form>"
        "</body></html>";

    httpd_resp_send(req, resp_str, strlen(resp_str));
    return ESP_OK;
}

esp_err_t get_lamps_get_handler(httpd_req_t *req)
{
    // Fetch lamp data from NVS and generate HTML content
    char *lamps_html = malloc(MAX_LAMPS * 600); // Dynamically allocate memory
    if (lamps_html == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for lamps_html");
        return ESP_FAIL;
    }
   
    lamps_html[0] = '\0';
    strncat(lamps_html, "<html><body><h1>Lamp Overview</h1><table><tr><th>Name</th><th>Address</th><th>Actions</th></tr>", MAX_LAMPS * 600 - 1);

    for (int i = 0; i < MAX_LAMPS; i++) {
    // Load lamp info from NVS
    LampInfo lamp_info;
    esp_err_t err = load_lamp_info(&lamp_info, i);
    if (err == ESP_OK) {
        // Append lamp data to HTML content
        char row[600];
        lamp_info.name[sizeof(lamp_info.name) - 1] = '\0';
        ESP_LOGI(TAG, "Adding lamp to table: Name=%s, Address=%s", lamp_info.name, lamp_info.address);
        snprintf(row, sizeof(row), "<tr><td>%s</td><td>%s</td><td><form action=\"/remove_lamp\" method=\"post\"><input type=\"hidden\" name=\"lamp_name\" value=\"%s\"><input type=\"hidden\" name=\"lamp_address\" value=\"%s\"><input type=\"submit\" value=\"Remove\"></form><form action=\"/edit_lamp\" method=\"get\"><input type=\"hidden\" name=\"lamp_name\" value=\"%s\"><input type=\"hidden\" name=\"lamp_address\" value=\"%s\"><input type=\"submit\" value=\"Edit\"></form></td></tr>", lamp_info.name, lamp_info.address, lamp_info.name, lamp_info.address,lamp_info.name,lamp_info.address);
        if (strlen(lamps_html) + strlen(row) < MAX_LAMPS * 600) {
            strncat(lamps_html, row, MAX_LAMPS * 600 - strlen(lamps_html) - 1);
        } else {
            ESP_LOGE(TAG, "Lamps HTML buffer full. Cannot append more rows.");
            break;  // Exit the loop if the buffer is full
        }
    }
    }

    // Close table and HTML tags
    strncat(lamps_html, "</table>", MAX_LAMPS * 600 - strlen(lamps_html) - 1);

    // Add "Add" button
    strncat(lamps_html, "<br><form action=\"/add_lamp_page\" method=\"get\"><input type=\"submit\" value=\"Add\"></form>", MAX_LAMPS * 600 - strlen(lamps_html) - 1);

    // Add "Restart" button
    strncat(lamps_html, "<br><form action=\"/restart\" method=\"get\"><input type=\"submit\" value=\"Restart\"></form>", MAX_LAMPS * 600 - strlen(lamps_html) - 1);

    // Close  HTML tags
    strncat(lamps_html, "</body></html>", MAX_LAMPS * 600 - strlen(lamps_html) - 1);

    // Send HTTP response with lamp overview HTML content
    httpd_resp_send(req, lamps_html, strlen(lamps_html));

    // Free dynamically allocated memory
    free(lamps_html);

    return ESP_OK;
}
// HTTP GET handler for the edit lamp page
esp_err_t edit_lamp_get_handler(httpd_req_t *req) {
    char lamp_name[100];  // Increase buffer size for lamp name
    char lamp_address[100];  // Increase buffer size for lamp address

    // Log the URI for debugging
    ESP_LOGI(TAG, "URI: %s", req->uri);

    // Parse query parameters to get lamp name and address
    char* query = strchr(req->uri, '?'); // Find the start of the query parameters
    if (query == NULL) {
        ESP_LOGE(TAG, "No query parameters found in the URI");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Move to the start of the query parameters
    query++;

    // Parse lamp name
    char* lamp_name_start = strstr(query, "lamp_name=");
    if (lamp_name_start == NULL) {
        ESP_LOGE(TAG, "lamp_name parameter not found in the URI");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    lamp_name_start += strlen("lamp_name="); // Move past "lamp_name="

    char* lamp_name_end = strchr(lamp_name_start, '&');
    if (lamp_name_end == NULL) {
        ESP_LOGE(TAG, "lamp_name parameter not terminated properly");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Copy lamp name
    int lamp_name_len = lamp_name_end - lamp_name_start;
    strncpy(lamp_name, lamp_name_start, lamp_name_len);
    lamp_name[lamp_name_len] = '\0'; // Null-terminate the string
    // Parse lamp address
    const char *lamp_address_ptr = strstr(query, "lamp_address=");
    if (lamp_address_ptr == NULL) {
        ESP_LOGE(TAG, "lamp_address parameter not found in URI");
        return ESP_FAIL;
    }
    lamp_address_ptr += strlen("lamp_address=");  // Move pointer to the start of the address value
    size_t address_length = strlen(lamp_address_ptr);  // Find the length of the address value until the end of the string
    if (address_length >= sizeof(lamp_address)) {
        ESP_LOGE(TAG, "lamp_address parameter value too long");
        return ESP_FAIL;
    }
    // Copy lamp address
    strncpy(lamp_address, lamp_address_ptr, address_length);  // Copy the address value
    lamp_address[address_length] = '\0';  // Null-terminate the address string

    // Log the lamp name and address for debugging
    ESP_LOGI(TAG, "Lamp Name: %s, Lamp Address: %s", lamp_name, lamp_address);
    // Respond with a simple message for testing
    // Generate HTML content for the edit lamp form
    char edit_page[600];
    snprintf(edit_page, sizeof(edit_page),
             "<html><body>"
             "<h1>Edit Lamp</h1>"
             "<form action=\"/update_lamp\" method=\"post\">"
             "<label for=\"lamp_name\">Lamp Name:</label>"
             "<input type=\"text\" id=\"lamp_name\" name=\"lamp_name\" value=\"%s\"><br><br>"
             "<label for=\"lamp_address\">Lamp Address:</label>"
             "<input type=\"text\" id=\"lamp_address\" name=\"lamp_address\" value=\"%s\"><br><br>"
             "<input type=\"submit\" value=\"Update Lamp\">"
             "</form>"
             "</body></html>",
             lamp_name, lamp_address);

    // Send HTTP response with the edit lamp form
    httpd_resp_send(req, edit_page, strlen(edit_page));

    return ESP_OK;
}


esp_err_t update_lamp_post_handler(httpd_req_t *req)
{
    char content[100];
    int ret, remaining = req->content_len;

    if (remaining > sizeof(content)) {
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "Content too long");
        return ESP_OK;
    }

    while (remaining > 0) {
        if ((ret = httpd_req_recv(req, content, MIN(remaining, sizeof(content)))) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        remaining -= ret;

        // Parse received data to get lamp name and address to update
        char lamp_name[50];
        char lamp_address_str[8];
        sscanf(content, "lamp_name=%[^&]&lamp_address=%6s", lamp_name, lamp_address_str);

        // Add your logic here to update the lamp
        ESP_LOGI(TAG, "Lamp Name: %s", lamp_name);
        ESP_LOGI(TAG, "Lamp Address: %s", lamp_address_str);
        
        // Find the index of the lamp to be updated
        int index_to_update = find_index_by_name_or_address(lamp_name, lamp_address_str);
        if (index_to_update >= 0) {
            // Perform the update operation, such as updating lamp information in NVS or elsewhere
            // For example:
            LampInfo updated_lamp;
            strncpy(updated_lamp.name, lamp_name, sizeof(updated_lamp.name));
            strncpy(updated_lamp.address, lamp_address_str, sizeof(updated_lamp.address));
            esp_err_t err = save_lamp_info(&updated_lamp, index_to_update);
            if (err == ESP_OK) {
                 ESP_LOGI(TAG, "Lamp updated successfully");
                 // Send a response indicating success
                 const char* resp_str =
                    "<html><head>"
                    "<script>window.location.replace('/');</script>"
                    "</head></html>";

                httpd_resp_send(req, resp_str, strlen(resp_str));
             } else {
                 ESP_LOGE(TAG, "Failed to update lamp: %d", err);
                 // Send a response indicating failure
                 httpd_resp_sendstr(req, "Failed to update lamp");
             }
        } else {
            // If the lamp to be updated is not found, send a response indicating failure
            ESP_LOGE(TAG, "Lamp not found for update");
            httpd_resp_sendstr(req, "Lamp not found for update");
        }
    }

    // Send a response indicating success after processing all received data
    httpd_resp_sendstr(req, "Update operation completed successfully");
    return ESP_OK;
}

esp_err_t restart_handler(httpd_req_t *req) {
    // Send response to the client
    const char* resp_str = "ESP32 is restarting...";
    httpd_resp_send(req, resp_str, strlen(resp_str));

    // Delay to ensure the response is sent before the restart
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Restart the ESP32
    esp_restart();

    return ESP_OK;
}

/* URI handlers */
httpd_uri_t add_lamp_uri = {
    .uri       = "/add_lamp",
    .method    = HTTP_POST,
    .handler   = add_lamp_post_handler,
    .user_ctx  = NULL
};
httpd_uri_t remove_lamp_uri = {
    .uri       = "/remove_lamp",
    .method    = HTTP_POST,
    .handler   = remove_lamp_post_handler,
    .user_ctx  = NULL
};
httpd_uri_t get_lamps_uri = {
    .uri       = "/get_lamps",
    .method    = HTTP_GET,
    .handler   = get_lamps_handler,
    .user_ctx  = NULL
};
httpd_uri_t edit_lamp_uri = {
    .uri       = "/edit_lamp",
    .method    = HTTP_GET,
    .handler   = edit_lamp_get_handler,
    .user_ctx  = NULL
};
httpd_uri_t add_lamp_get_uri = {
    .uri       = "/add_lamp_page",
    .method    = HTTP_GET,
    .handler   = add_lamp_get_handler,
    .user_ctx  = NULL
};
httpd_uri_t update_lamp_post_uri = {
    .uri       = "/update_lamp",
    .method    = HTTP_POST,
    .handler   = update_lamp_post_handler,
    .user_ctx  = NULL
};
httpd_uri_t restart_uri = {
    .uri       = "/restart",
    .method    = HTTP_GET,
    .handler   = restart_handler,
    .user_ctx  = NULL
};
// Add overview_uri as the default URI handler
httpd_uri_t default_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = get_lamps_get_handler,  // Handler for the overview page
    .user_ctx  = NULL
};
// Initialize HTTP server
httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    // Start the httpd server
    if (httpd_start(&server, &config) == ESP_OK) {
        // Register URI handlers
        httpd_register_uri_handler(server, &add_lamp_uri);
        httpd_register_uri_handler(server, &remove_lamp_uri);
        httpd_register_uri_handler(server, &get_lamps_uri);
        httpd_register_uri_handler(server, &add_lamp_get_uri);
        httpd_register_uri_handler(server, &restart_uri);
        httpd_register_uri_handler(server, &default_uri);
        httpd_register_uri_handler(server, &edit_lamp_uri);
        httpd_register_uri_handler(server, &update_lamp_post_uri);
        
    }

    // Return the server handle
    return server;
}


// Stop the web server
void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}
