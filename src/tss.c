/*
 * tss.c
 * Functions for communicating with Apple's TSS server
 *
 * Copyright (c) 2010-2013 Martin Szulecki. All Rights Reserved.
 * Copyright (c) 2012 Nikias Bassen. All Rights Reserved.
 * Copyright (c) 2010 Joshua Hill. All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include <plist/plist.h>

#include "tss.h"
#include "img3.h"
#include "common.h"
#include "idevicerestore.h"

#define TSS_CLIENT_VERSION_STRING "libauthinstall-293.1.16"
#define ECID_STRSIZE 0x20

typedef struct {
	int length;
	char* content;
} tss_response;

char* ecid_to_string(uint64_t ecid) {
	char* ecid_string = malloc(ECID_STRSIZE);
	memset(ecid_string, '\0', ECID_STRSIZE);
	if (ecid == 0) {
		error("ERROR: Invalid ECID passed.\n");
		return NULL;
	}
	snprintf(ecid_string, ECID_STRSIZE, FMT_qu, (long long unsigned int)ecid);
	return ecid_string;
}

plist_t tss_request_new(plist_t overrides) {

	plist_t request = plist_new_dict();

	plist_dict_insert_item(request, "@Locality", plist_new_string("en_US"));
	plist_dict_insert_item(request, "@HostPlatformInfo",
#ifdef WIN32
		plist_new_string("windows")
#else
		plist_new_string("mac")
#endif
	);

	plist_dict_insert_item(request, "@VersionInfo", plist_new_string(TSS_CLIENT_VERSION_STRING));
	char* guid = generate_guid();
	if (guid) {
		plist_dict_insert_item(request, "@UUID", plist_new_string(guid));
		free(guid);
	}

	/* apply overrides */
	if (overrides) {
		plist_dict_merge(&request, overrides);
	}

	return request;
}

int tss_request_add_ap_img4_tags(plist_t request, plist_t parameters) {
	plist_t node = NULL;

	if (!parameters) {
		error("ERROR: Missing required AP parameters\n");
		return -1;
	}

	/* ApNonce */
	node = plist_dict_get_item(parameters, "ApNonce");
	if (!node || plist_get_node_type(node) != PLIST_DATA) {
		error("ERROR: Unable to find required ApNonce in parameters\n");
		return -1;
	}
	plist_dict_insert_item(request, "ApNonce", plist_copy(node));
	node = NULL;

	plist_dict_insert_item(request, "@ApImg4Ticket", plist_new_bool(1));

	/* ApSecurityMode */
	node = plist_dict_get_item(request, "ApSecurityMode");
	if (!node) {
		/* copy from parameters if available */
		node = plist_dict_get_item(parameters, "ApSecurityMode");
		if (!node || plist_get_node_type(node) != PLIST_UINT) {
			error("ERROR: Unable to find required ApSecurityMode in parameters\n");
			return -1;
		}
		plist_dict_insert_item(request, "ApSecurityMode", plist_copy(node));
		node = NULL;
	}

	/* ApSepNonce */
	node = plist_dict_get_item(parameters, "ApSepNonce");
	if (!node || plist_get_node_type(node) != PLIST_DATA) {
		error("ERROR: Unable to find required ApSepNonce in parameters\n");
		return -1;
	}
	plist_dict_insert_item(request, "ApSepNonce", plist_copy(node));
	node = NULL;

	return 0;
}

int tss_request_add_ap_img3_tags(plist_t request, plist_t parameters) {
	plist_t node = NULL;

	if (!parameters) {
		error("ERROR: Missing required AP parameters\n");
		return -1;
	}

	/* ApNonce */
	node = plist_dict_get_item(parameters, "ApNonce");
	if (node) {
		if (plist_get_node_type(node) != PLIST_DATA) {
			error("ERROR: Unable to find required ApNonce in parameters\n");
			return -1;
		}
		plist_dict_insert_item(request, "ApNonce", plist_copy(node));
		node = NULL;
	}

	/* @APTicket */
	plist_dict_insert_item(request, "@APTicket", plist_new_bool(1));

	/* ApECID */
	node = plist_dict_get_item(parameters, "ApECID");
	if (!node || plist_get_node_type(node) != PLIST_UINT) {
		error("ERROR: Unable to find required ApECID in parameters\n");
		return -1;
	}
	plist_dict_insert_item(request, "ApECID", plist_copy(node));
	node = NULL;

	/* ApBoardID */
	node = plist_dict_get_item(request, "ApBoardID");
	if (!node || plist_get_node_type(node) != PLIST_UINT) {
		error("ERROR: Unable to find required ApBoardID in request\n");
		return -1;
	}
	node = NULL;

	/* ApChipID */
	node = plist_dict_get_item(request, "ApChipID");
	if (!node || plist_get_node_type(node) != PLIST_UINT) {
		error("ERROR: Unable to find required ApChipID in request\n");
		return -1;
	}
	node = NULL;

	/* ApSecurityDomain */
	node = plist_dict_get_item(request, "ApSecurityDomain");
	if (!node || plist_get_node_type(node) != PLIST_UINT) {
		error("ERROR: Unable to find required ApSecurityDomain in request\n");
		return -1;
	}
	node = NULL;

	/* ApProductionMode */
	node = plist_dict_get_item(parameters, "ApProductionMode");
	if (!node || plist_get_node_type(node) != PLIST_BOOLEAN) {
		error("ERROR: Unable to find required ApProductionMode in parameters\n");
		return -1;
	}
	plist_dict_insert_item(request, "ApProductionMode", plist_copy(node));
	node = NULL;

	return 0;
}

int tss_request_add_baseband_tags(plist_t request, plist_t parameters) {
	plist_t node = NULL;

	if (!parameters) {
		error("ERROR: Missing required AP parameters\n");
		return -1;
	}

	/* BbNonce */
	node = plist_dict_get_item(parameters, "BbNonce");
	if (!node || plist_get_node_type(node) != PLIST_DATA) {
		error("ERROR: Unable to find required BbNonce in parameters\n");
		return -1;
	}
	plist_dict_insert_item(request, "BbNonce", plist_copy(node));
	node = NULL;

	/* @BBTicket */
	plist_dict_insert_item(request, "@BBTicket", plist_new_bool(1));

	/* BbGoldCertId */
	node = plist_dict_get_item(parameters, "BbGoldCertId");
	if (!node || plist_get_node_type(node) != PLIST_UINT) {
		error("ERROR: Unable to find required BbGoldCertId in parameters\n");
		return -1;
	}
	plist_dict_insert_item(request, "BbGoldCertId", plist_copy(node));
	node = NULL;

	/* BbSNUM */
	node = plist_dict_get_item(parameters, "BbSNUM");
	if (!node || plist_get_node_type(node) != PLIST_DATA) {
		error("ERROR: Unable to find required BbSNUM in parameters\n");
		return -1;
	}
	plist_dict_insert_item(request, "BbSNUM", plist_copy(node));
	node = NULL;

	return 0;
}

int tss_request_add_ap_tags_from_manifest(plist_t request, plist_t build_identity, plist_t overrides) {
	plist_t node = NULL;
	char* string = NULL;

	/* UniqueBuildID */
	char* unique_build_data = NULL;
	uint64_t unique_build_size = 0;
	node = plist_dict_get_item(build_identity, "UniqueBuildID");
	if (!node || plist_get_node_type(node) != PLIST_DATA) {
		error("ERROR: Unable to find UniqueBuildID node\n");
		return -1;
	}
	plist_get_data_val(node, &unique_build_data, &unique_build_size);
	plist_dict_insert_item(request, "UniqueBuildID", plist_new_data(unique_build_data, unique_build_size));
	free(unique_build_data);
	node = NULL;

	/* ApChipID */
	int chip_id = 0;
	node = plist_dict_get_item(build_identity, "ApChipID");
	if (!node || plist_get_node_type(node) != PLIST_STRING) {
		error("ERROR: Unable to find ApChipID node\n");
		return -1;
	}
	plist_get_string_val(node, &string);
	sscanf(string, "%x", &chip_id);
	plist_dict_insert_item(request, "ApChipID", plist_new_uint(chip_id));
	free(string);
	string = NULL;
	node = NULL;

	/* ApBoardID */
	int board_id = 0;
	node = plist_dict_get_item(build_identity, "ApBoardID");
	if (!node || plist_get_node_type(node) != PLIST_STRING) {
		error("ERROR: Unable to find ApBoardID node\n");
		return -1;
	}
	plist_get_string_val(node, &string);
	sscanf(string, "%x", &board_id);
	plist_dict_insert_item(request, "ApBoardID", plist_new_uint(board_id));
	free(string);
	string = NULL;
	node = NULL;

	/* ApSecurityDomain */
	int security_domain = 0;
	node = plist_dict_get_item(build_identity, "ApSecurityDomain");
	if (!node || plist_get_node_type(node) != PLIST_STRING) {
		error("ERROR: Unable to find ApSecurityDomain node\n");
		return -1;
	}
	plist_get_string_val(node, &string);
	sscanf(string, "%x", &security_domain);
	plist_dict_insert_item(request, "ApSecurityDomain", plist_new_uint(security_domain));
	free(string);
	string = NULL;
	node = NULL;

	/* loop over components from build manifest */
	plist_t manifest_node = plist_dict_get_item(build_identity, "Manifest");
	if (!manifest_node || plist_get_node_type(manifest_node) != PLIST_DICT) {
		error("ERROR: Unable to find restore manifest\n");
		return -1;
	}

	/* add components to request */
	char* key = NULL;
	plist_t manifest_entry = NULL;
	plist_dict_iter iter = NULL;
	plist_dict_new_iter(manifest_node, &iter);
	while (1) {
		plist_dict_next_item(manifest_node, iter, &key, &manifest_entry);
		if (key == NULL)
			break;
		if (!manifest_entry || plist_get_node_type(manifest_entry) != PLIST_DICT) {
			error("ERROR: Unable to fetch BuildManifest entry\n");
			return -1;
		}

		/* do not populate BaseBandFirmware, only in basebaseband request */
		if ((strcmp(key, "BasebandFirmware") == 0)) {
			free(key);
			continue;
		}

		/* FIXME: only used with diagnostics firmware */
		if ((strcmp(key, "Diags") == 0) || (strcmp(key, "OS") == 0)) {
			free(key);
			continue;
		}

		/* copy this entry to request */
		plist_t tss_entry = plist_copy(manifest_entry);

		/* remove obsolete Info node */
		plist_dict_remove_item(tss_entry, "Info");

		/* FIXME: If ApSupportsImg4 */
		plist_dict_insert_item(tss_entry, "EPRO", plist_new_bool(1));
		plist_dict_insert_item(tss_entry, "ESEC", plist_new_bool(1));

		plist_dict_insert_item(request, key, tss_entry);

		free(key);
	}

	/* apply overrides */
	if (overrides) {
		plist_dict_merge(&request, overrides);
	}

	return 0;
}

int tss_request_add_baseband_tags_from_manifest(plist_t request, plist_t build_identity, plist_t overrides) {
	plist_t bb_node = NULL;

	/* BbChipID */
	int bb_chip_id = 0;
	char* bb_chip_id_string = NULL;
	bb_node = plist_dict_get_item(build_identity, "BbChipID");
	if (!bb_node || plist_get_node_type(bb_node) != PLIST_STRING) {
		error("ERROR: Unable to find BbChipID node\n");
		return -1;
	}
	plist_get_string_val(bb_node, &bb_chip_id_string);
	sscanf(bb_chip_id_string, "%x", &bb_chip_id);
	plist_dict_insert_item(request, "BbChipID", plist_new_uint(bb_chip_id));
	bb_node = NULL;

	/* BbProvisioningManifestKeyHash */
	bb_node = plist_dict_get_item(build_identity, "BbProvisioningManifestKeyHash");
	if (bb_node && plist_get_node_type(bb_node) == PLIST_DATA) {
		plist_dict_insert_item(request, "BbProvisioningManifestKeyHash", plist_copy(bb_node));
	} else {
		error("WARNING: Unable to find BbProvisioningManifestKeyHash node\n");
	}
	bb_node = NULL;

	/* BbActivationManifestKeyHash - Used by Qualcomm MDM6610 */
	bb_node = plist_dict_get_item(build_identity, "BbActivationManifestKeyHash");
	if (bb_node && plist_get_node_type(bb_node) == PLIST_DATA) {
		plist_dict_insert_item(request, "BbActivationManifestKeyHash", plist_copy(bb_node));
	} else {
		error("WARNING: Unable to find BbActivationManifestKeyHash node\n");
	}
	bb_node = NULL;

	bb_node = plist_dict_get_item(build_identity, "BbCalibrationManifestKeyHash");
	if (bb_node && plist_get_node_type(bb_node) == PLIST_DATA) {
		plist_dict_insert_item(request, "BbCalibrationManifestKeyHash", plist_copy(bb_node));
	} else {
		error("WARNING: Unable to find BbCalibrationManifestKeyHash node\n");
	}
	bb_node = NULL;

	/* BbFactoryActivationManifestKeyHash */
	bb_node = plist_dict_get_item(build_identity, "BbFactoryActivationManifestKeyHash");
	if (bb_node && plist_get_node_type(bb_node) == PLIST_DATA) {
		plist_dict_insert_item(request, "BbFactoryActivationManifestKeyHash", plist_copy(bb_node));
	} else {
		error("WARNING: Unable to find BbFactoryActivationManifestKeyHash node\n");
	}
	bb_node = NULL;

	/* BbSkeyId - Used by XMM 6180/GSM */
	bb_node = plist_dict_get_item(build_identity, "BbSkeyId");
	if (bb_node && plist_get_node_type(bb_node) == PLIST_DATA) {
		plist_dict_insert_item(request, "BbSkeyId", plist_copy(bb_node));
	} else {
		error("WARNING: Unable to find BbSkeyId node\n");
	}
	bb_node = NULL;

	/* BasebandFirmware */
	bb_node = plist_access_path(build_identity, 2, "Manifest", "BasebandFirmware");
	if (!bb_node || plist_get_node_type(bb_node) != PLIST_DICT) {
		error("ERROR: Unable to get BasebandFirmware node\n");
		return -1;
	}
	plist_dict_insert_item(request, "BasebandFirmware", plist_copy(bb_node));
	bb_node = NULL;

	/* apply overrides */
	if (overrides) {
		plist_dict_merge(&request, overrides);
	}

	return 0;
}

size_t tss_write_callback(char* data, size_t size, size_t nmemb, tss_response* response) {
	size_t total = size * nmemb;
	if (total != 0) {
		response->content = realloc(response->content, response->length + total + 1);
		memcpy(response->content + response->length, data, total);
		response->content[response->length + total] = '\0';
		response->length += total;
	}

	return total;
}

plist_t tss_request_send(plist_t tss_request, const char* server_url_string) {
	curl_global_init(CURL_GLOBAL_ALL);

	if (idevicerestore_debug) {
		debug_plist(tss_request);
	}

	char* request = NULL;
	int status_code = -1;
	int retry = 0;
	int max_retries = 15;
	unsigned int size = 0;
	char curl_error_message[CURL_ERROR_SIZE];

	const char* urls[6] = {
		"https://gs.apple.com/TSS/controller?action=2",
		"https://17.171.36.30/TSS/controller?action=2",
		"https://17.151.36.30/TSS/controller?action=2",
		"http://gs.apple.com/TSS/controller?action=2",
		"http://17.171.36.30/TSS/controller?action=2",
		"http://17.151.36.30/TSS/controller?action=2"
	};

	plist_to_xml(tss_request, &request, &size);

	tss_response* response = NULL;
	memset(curl_error_message, '\0', CURL_ERROR_SIZE);

	while (retry++ < max_retries) {
		response = NULL;
		CURL* handle = curl_easy_init();
		if (handle == NULL) {
			break;
		}
		struct curl_slist* header = NULL;
		header = curl_slist_append(header, "Cache-Control: no-cache");
		header = curl_slist_append(header, "Content-type: text/xml; charset=\"utf-8\"");
		header = curl_slist_append(header, "Expect:");

		response = malloc(sizeof(tss_response));
		if (response == NULL) {
			fprintf(stderr, "Unable to allocate sufficent memory\n");
			return NULL;
		}

		response->length = 0;
		response->content = malloc(1);
		response->content[0] = '\0';

		/* disable SSL verification to allow download from untrusted https locations */
		curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0);

		curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, curl_error_message);
		curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, (curl_write_callback)&tss_write_callback);
		curl_easy_setopt(handle, CURLOPT_WRITEDATA, response);
		curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header);
		curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request);
		curl_easy_setopt(handle, CURLOPT_USERAGENT, "InetURL/1.0");
		curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, strlen(request));
		if (server_url_string) {
			curl_easy_setopt(handle, CURLOPT_URL, server_url_string);
		} else {
			int url_index = (retry - 1) % 6;
			curl_easy_setopt(handle, CURLOPT_URL, urls[url_index]);
			info("Request URL set to %s\n", urls[url_index]);
		}

		info("Sending TSS request attempt %d... ", retry);

		curl_easy_perform(handle);
		curl_slist_free_all(header);
		curl_easy_cleanup(handle);
	
		if (strstr(response->content, "MESSAGE=SUCCESS")) {
			status_code = 0;
			info("response successfully received\n");
			break;
		}

		if (response->length > 0) {
			error("TSS server returned: %s\n", response->content);
		}

		char* status = strstr(response->content, "STATUS=");
		if (status) {
			sscanf(status+7, "%d&%*s", &status_code);
		}
		if (status_code == -1) {
			error("%s\n", curl_error_message);
			// no status code in response. retry
			free(response->content);
			free(response);
			sleep(2);
			continue;
		} else if (status_code == 8) {
			// server error (invalid bb request?)
			break;
		} else if (status_code == 49) {
			// server error (invalid bb data, e.g. BbSNUM?)
			break;
		} else if (status_code == 94) {
			// This device isn't eligible for the requested build.
			break;
		} else if (status_code == 100) {
			// server error, most likely the request was malformed
			break;
		} else {
			error("ERROR: tss_send_request: Unhandled status code %d\n", status_code);
		}
	}

	if (status_code != 0) {
		if (strstr(response->content, "MESSAGE=") != NULL) {
			char* message = strstr(response->content, "MESSAGE=") + strlen("MESSAGE=");
			error("ERROR: TSS request failed (status=%d, message=%s)\n", status_code, message);
		} else {
			error("ERROR: TSS request failed: %s (status=%d)\n", curl_error_message, status_code);
		}
		free(request);
		free(response->content);
		free(response);
		return NULL;
	}

	char* tss_data = strstr(response->content, "<?xml");
	if (tss_data == NULL) {
		error("ERROR: Incorrectly formatted TSS response\n");
		free(request);
		free(response->content);
		free(response);
		return NULL;
	}

	uint32_t tss_size = 0;
	plist_t tss_response = NULL;
	tss_size = response->length - (tss_data - response->content);
	plist_from_xml(tss_data, tss_size, &tss_response);
	free(response->content);
	free(response);

	if (idevicerestore_debug) {
		debug_plist(tss_response);
	}

	free(request);
	curl_global_cleanup();

	return tss_response;
}

static int tss_response_get_data_by_key(plist_t response, const char* name, unsigned char** buffer, unsigned int* length) {

	plist_t node = plist_dict_get_item(response, name);
	if (!node || plist_get_node_type(node) != PLIST_DATA) {
		error("ERROR: Unable to find %s entry in TSS response\n", name);
		return -1;
	}

	char *data = NULL;
	uint64_t len = 0;
	plist_get_data_val(node, &data, &len);
	if (data) {
		*length = (unsigned int)len;
		*buffer = (unsigned char*)data;
		return 0;
	} else {
		error("ERROR: Unable to get %s data from TSS response\n", name);
		return -1;
	}
}

int tss_response_get_ap_img4_ticket(plist_t response, unsigned char** ticket, unsigned int* length) {
	return tss_response_get_data_by_key(response, "APImg4Ticket", ticket, length);
}

int tss_response_get_ap_ticket(plist_t response, unsigned char** ticket, unsigned int* length) {
	return tss_response_get_data_by_key(response, "APTicket", ticket, length);
}

int tss_response_get_baseband_ticket(plist_t response, unsigned char** ticket, unsigned int* length) {
	return tss_response_get_data_by_key(response, "BBTicket", ticket, length);
}

int tss_response_get_path_by_entry(plist_t response, const char* entry, char** path) {
	char* path_string = NULL;
	plist_t path_node = NULL;
	plist_t entry_node = NULL;

	*path = NULL;

	entry_node = plist_dict_get_item(response, entry);
	if (!entry_node || plist_get_node_type(entry_node) != PLIST_DICT) {
		error("ERROR: Unable to find %s entry in TSS response\n", entry);
		return -1;
	}

	path_node = plist_dict_get_item(entry_node, "Path");
	if (!path_node || plist_get_node_type(path_node) != PLIST_STRING) {
		debug("NOTE: Unable to find %s path in TSS entry\n", entry);
		return -1;
	}
	plist_get_string_val(path_node, &path_string);

	*path = path_string;
	return 0;
}

int tss_response_get_blob_by_path(plist_t tss, const char* path, unsigned char** blob) {
	int i = 0;
	uint32_t tss_size = 0;
	uint64_t blob_size = 0;
	char* entry_key = NULL;
	char* blob_data = NULL;
	char* entry_path = NULL;
	plist_t tss_entry = NULL;
	plist_t blob_node = NULL;
	plist_t path_node = NULL;
	plist_dict_iter iter = NULL;

	*blob = NULL;

	plist_dict_new_iter(tss, &iter);
	tss_size = plist_dict_get_size(tss);
	for (i = 0; i < tss_size; i++) {
		plist_dict_next_item(tss, iter, &entry_key, &tss_entry);
		if (entry_key == NULL)
			break;

		if (!tss_entry || plist_get_node_type(tss_entry) != PLIST_DICT) {
			continue;
		}

		path_node = plist_dict_get_item(tss_entry, "Path");
		if (!path_node || plist_get_node_type(path_node) != PLIST_STRING) {
			error("ERROR: Unable to find TSS path node in entry %s\n", entry_key);
			return -1;
		}

		plist_get_string_val(path_node, &entry_path);
		if (strcmp(path, entry_path) == 0) {
			blob_node = plist_dict_get_item(tss_entry, "Blob");
			if (!blob_node || plist_get_node_type(blob_node) != PLIST_DATA) {
				error("ERROR: Unable to find TSS blob node in entry %s\n", entry_key);
				return -1;
			}
			plist_get_data_val(blob_node, &blob_data, &blob_size);
			break;
		}

		free(entry_key);
	}

	if (blob_data == NULL || blob_size <= 0) {
		return -1;
	}

	*blob = (unsigned char*)blob_data;
	return 0;
}

int tss_response_get_blob_by_entry(plist_t response, const char* entry, unsigned char** blob) {
	uint64_t blob_size = 0;
	char* blob_data = NULL;
	plist_t blob_node = NULL;
	plist_t tss_entry = NULL;

	*blob = NULL;

	tss_entry = plist_dict_get_item(response, entry);
	if (!tss_entry || plist_get_node_type(tss_entry) != PLIST_DICT) {
		error("ERROR: Unable to find %s entry in TSS response\n", entry);
		return -1;
	}

	blob_node = plist_dict_get_item(tss_entry, "Blob");
	if (!blob_node || plist_get_node_type(blob_node) != PLIST_DATA) {
		error("ERROR: Unable to find blob in %s entry\n", entry);
		return -1;
	}
	plist_get_data_val(blob_node, &blob_data, &blob_size);

	*blob = (unsigned char*)blob_data;
	return 0;
}
