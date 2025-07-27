#include "http_server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include "cJSON.h"
#include "main.h"
#include "lamp_nvs.h"

#define TAG "HTTP_SERVER"

// Helper function to parse URL-encoded form data
static esp_err_t get_post_field(char *buf, const char *field, char *dest, int dest_len)
{
    const char *start = strstr(buf, field);
    if (!start) {
        return ESP_FAIL;
    }
    start += strlen(field); // Move pointer past "field="
    char *end = strchr(start, '&');
    size_t len;
    if (end) {
        len = end - start;
    } else {
        len = strlen(start);
    }

    if (len >= dest_len) {
        return ESP_FAIL; // Destination buffer too small
    }

    memcpy(dest, start, len);
    dest[len] = '\0';
    // Basic URL decoding for spaces ('+' -> ' ') and other chars might be needed for robustness
    char *p = dest;
    while ((p = strchr(p, '+')) != NULL) {
        *p = ' ';
    }
    return ESP_OK;
}

/* An HTTP POST handler for adding a new lamp */
static esp_err_t add_lamp_post_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    LampInfo new_lamp;
    if (get_post_field(buf, "lamp_name=", new_lamp.name, sizeof(new_lamp.name)) != ESP_OK ||
        get_post_field(buf, "lamp_address=", new_lamp.address, sizeof(new_lamp.address)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form data");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Adding lamp: Name='%s', Address='%s'", new_lamp.name, new_lamp.address);

    esp_err_t err = add_lamp_info(&new_lamp);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Lamp added successfully.");
        refresh_mqtt_subscriptions();
        publish_ha_discovery_messages();
    } else {
        ESP_LOGE(TAG, "Failed to add lamp: %s", esp_err_to_name(err));
    }

    // Redirect back to the main page
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* An HTTP POST handler for removing a lamp */
static esp_err_t remove_lamp_post_handler(httpd_req_t *req)
{
    char buf[100];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) { return ESP_FAIL; }
    buf[ret] = '\0';

    char lamp_name[MAX_LAMP_NAME_LEN];
    if (get_post_field(buf, "lamp_name=", lamp_name, sizeof(lamp_name)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing lamp_name");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Removing lamp: Name='%s'", lamp_name);

    esp_err_t err = remove_lamp_info_by_name(lamp_name);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Lamp removed successfully.");
        refresh_mqtt_subscriptions();
        publish_ha_discovery_messages();
    } else {
        ESP_LOGE(TAG, "Failed to remove lamp: %s", esp_err_to_name(err));
    }

    // Redirect back to the main page
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* An HTTP POST handler for updating a lamp */
static esp_err_t update_lamp_post_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) { return ESP_FAIL; }
    buf[ret] = '\0';

    char original_name[MAX_LAMP_NAME_LEN];
    LampInfo updated_lamp;

    if (get_post_field(buf, "original_name=", original_name, sizeof(original_name)) != ESP_OK ||
        get_post_field(buf, "lamp_name=", updated_lamp.name, sizeof(updated_lamp.name)) != ESP_OK ||
        get_post_field(buf, "lamp_address=", updated_lamp.address, sizeof(updated_lamp.address)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form data");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Updating lamp '%s' to Name='%s', Address='%s'", original_name, updated_lamp.name, updated_lamp.address);

    esp_err_t err = update_lamp_info(original_name, &updated_lamp);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Lamp updated successfully.");
        refresh_mqtt_subscriptions();
        publish_ha_discovery_messages();
    } else {
        ESP_LOGE(TAG, "Failed to update lamp: %s", esp_err_to_name(err));
    }

    // Redirect back to the main page
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}


/* An HTTP GET handler for the main lamp overview page */
static esp_err_t get_lamps_overview_handler(httpd_req_t *req)
{
    httpd_resp_sendstr_chunk(req,
        "<html><head><title>BLE Mesh Gateway</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f4f4f9; }"
        "h1, h2 { color: #333; }"
        "table { width: 100%; border-collapse: collapse; margin-top: 20px; }"
        "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }"
        "th { background-color: #4CAF50; color: white; }"
        "tr:nth-child(even) { background-color: #f2f2f2; }"
        "form { display: inline-block; margin: 0; }"
        "input[type=submit] { background-color: #4CAF50; color: white; padding: 5px 10px; border: none; border-radius: 4px; cursor: pointer; }"
        "input[type=submit].remove { background-color: #f44336; }"
        "input[type=text] { padding: 5px; border-radius: 4px; border: 1px solid #ccc; }"
        ".container { background-color: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }"
        "</style></head><body><div class='container'>"
        "<h1>Lamp Overview</h1><table>"
        "<tr><th>Name</th><th>Address</th><th>Actions</th></tr>");

    int lamp_count;
    const LampInfo* lamps = get_all_lamps(&lamp_count);

    for (int i = 0; i < lamp_count; i++) {
        char row_buf[512];
        snprintf(row_buf, sizeof(row_buf),
            "<tr><td>%s</td><td>%s</td><td>"
            "<form action='/remove_lamp' method='post'>"
            "<input type='hidden' name='lamp_name' value='%s'>"
            "<input type='submit' value='Remove' class='remove'>"
            "</form> "
            "<form action='/edit_lamp' method='get'>"
            "<input type='hidden' name='lamp_name' value='%s'>"
            "<input type='submit' value='Edit'>"
            "</form>"
            "</td></tr>",
            lamps[i].name, lamps[i].address, lamps[i].name, lamps[i].name);
        httpd_resp_sendstr_chunk(req, row_buf);
    }

    httpd_resp_sendstr_chunk(req, "</table>");

    httpd_resp_sendstr_chunk(req,
        "<h2>Add New Lamp</h2>"
        "<form action='/add_lamp' method='post'>"
        "<label for='lamp_name'>Name: </label>"
        "<input type='text' id='lamp_name' name='lamp_name' required> "
        "<label for='lamp_address'>Address: </label>"
        "<input type='text' id='lamp_address' name='lamp_address' placeholder='e.g., 0x0019' required> "
        "<input type='submit' value='Add Lamp'>"
        "</form>"
        "<h2>System</h2>"
        "<form action='/restart' method='post'><input type='submit' value='Restart Device'></form>"
        "</div></body></html>");

    // Final chunk to end the response
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* An HTTP GET handler to serve the edit lamp page */
static esp_err_t edit_lamp_get_handler(httpd_req_t *req)
{
    char query_buf[128];
    char lamp_name[MAX_LAMP_NAME_LEN];
    LampInfo lamp_to_edit;

    if (httpd_req_get_url_query_str(req, query_buf, sizeof(query_buf)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing query string");
        return ESP_FAIL;
    }
    if (httpd_query_key_value(query_buf, "lamp_name", lamp_name, sizeof(lamp_name)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing lamp_name parameter");
        return ESP_FAIL;
    }

    if (find_lamp_by_name(lamp_name, &lamp_to_edit) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Lamp not found");
        return ESP_FAIL;
    }

    // Send the response in chunks to avoid large stack buffers and compiler warnings
    httpd_resp_sendstr_chunk(req,
        "<html><head><title>Edit Lamp</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f4f4f9; }"
        "h1 { color: #333; }"
        ".container { background-color: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }"
        "input[type=text], input[type=submit] { padding: 8px; margin-top: 5px; border-radius: 4px; border: 1px solid #ccc; }"
        "input[type=submit] { background-color: #4CAF50; color: white; cursor: pointer; }"
        "</style></head><body><div class='container'>");

    char chunk_buf[512]; // A reasonably sized buffer for dynamic parts
    
    // Send header with lamp name
    snprintf(chunk_buf, sizeof(chunk_buf), "<h1>Edit Lamp: %s</h1>", lamp_to_edit.name);
    httpd_resp_sendstr_chunk(req, chunk_buf);

    // Send form part 1
    snprintf(chunk_buf, sizeof(chunk_buf),
             "<form action='/update_lamp' method='post'>"
             "<input type='hidden' name='original_name' value='%s'>"
             "<label for='lamp_name'>New Name:</label><br>"
             "<input type='text' id='lamp_name' name='lamp_name' value='%s' required><br><br>",
             lamp_to_edit.name, lamp_to_edit.name);
    httpd_resp_sendstr_chunk(req, chunk_buf);

    // Send form part 2
    snprintf(chunk_buf, sizeof(chunk_buf),
             "<label for='lamp_address'>New Address:</label><br>"
             "<input type='text' id='lamp_address' name='lamp_address' value='%s' required><br><br>"
             "<input type='submit' value='Update Lamp'>"
             "</form><br><a href='/'>Back to Overview</a>"
             "</div></body></html>",
             lamp_to_edit.address);
    httpd_resp_sendstr_chunk(req, chunk_buf);

    // Send final chunk to close connection
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}

/* An HTTP POST handler to restart the device */
static esp_err_t restart_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Restarting device via HTTP request.");
    httpd_resp_sendstr(req, "Restarting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard; // Allow wildcard matching

    ESP_LOGI(TAG, "Starting httpd server");
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start httpd server");
        return NULL;
    }

    // URI Handlers
    const httpd_uri_t root = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = get_lamps_overview_handler,
    };
    httpd_register_uri_handler(server, &root);

    const httpd_uri_t add_lamp = {
        .uri       = "/add_lamp",
        .method    = HTTP_POST,
        .handler   = add_lamp_post_handler,
    };
    httpd_register_uri_handler(server, &add_lamp);

    const httpd_uri_t remove_lamp = {
        .uri       = "/remove_lamp",
        .method    = HTTP_POST,
        .handler   = remove_lamp_post_handler,
    };
    httpd_register_uri_handler(server, &remove_lamp);
    
    const httpd_uri_t edit_lamp = {
        .uri       = "/edit_lamp",
        .method    = HTTP_GET,
        .handler   = edit_lamp_get_handler,
    };
    httpd_register_uri_handler(server, &edit_lamp);

    const httpd_uri_t update_lamp = {
        .uri       = "/update_lamp",
        .method    = HTTP_POST,
        .handler   = update_lamp_post_handler,
    };
    httpd_register_uri_handler(server, &update_lamp);

    const httpd_uri_t restart = {
        .uri       = "/restart",
        .method    = HTTP_POST,
        .handler   = restart_post_handler,
    };
    httpd_register_uri_handler(server, &restart);

    return server;
}
