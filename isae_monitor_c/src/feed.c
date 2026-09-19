/**
 * ISAE Monitor - Feed Parser Implementation
 * 
 * Maps Python: feed.py -> C: feed.c
 * 
 * Fetches and parses RSS/Atom feeds using libxml2.
 */

#include "isae_monitor/feed.h"
#include "isae_monitor/httpclient.h"
#include <libxml/parser.h>
#include <libxml/tree.h>

void feed_result_init(feed_result_t* result) {
    if (!result) return;
    
    result->entries = NULL;
    result->count = 0;
    result->feed_title[0] = '\0';
    result->feed_url[0] = '\0';
}

void feed_result_cleanup(feed_result_t* result) {
    if (!result) return;
    
    if (result->entries) {
        for (size_t i = 0; i < result->count; i++) {
            /* Entries use fixed-size arrays, nothing to free */
        }
        free(result->entries);
        result->entries = NULL;
    }
    result->count = 0;
}

static xmlChar* get_node_content(xmlNode* node) {
    if (!node) return NULL;
    
    xmlChar* content = xmlNodeGetContent(node);
    if (content) {
        return content;
    }
    
    /* Try children */
    for (xmlNode* child = node->children; child; child = child->next) {
        content = xmlNodeGetContent(child);
        if (content && xmlStrlen(content) > 0) {
            return content;
        }
    }
    
    return NULL;
}

static void parse_atom_entry(xmlNode* entry_node, feed_entry_t* entry) {
    memset(entry, 0, sizeof(feed_entry_t));
    
    for (xmlNode* node = entry_node->children; node; node = node->next) {
        xmlChar* content;
        
        if (xmlStrcmp(node->name, (const xmlChar*)"title") == 0) {
            content = get_node_content(node);
            if (content) {
                char* unescaped = malloc(xmlStrlen(content) + 1);
                if (unescaped) {
                    html_unescape((char*)content, unescaped, xmlStrlen(content) + 1);
                    strncpy(entry->title, unescaped, MAX_TITLE_LEN - 1);
                    free(unescaped);
                }
                xmlFree(content);
            }
        } else if (xmlStrcmp(node->name, (const xmlChar*)"summary") == 0 ||
                   xmlStrcmp(node->name, (const xmlChar*)"content") == 0) {
            content = get_node_content(node);
            if (content) {
                char* unescaped = malloc(xmlStrlen(content) + 1);
                if (unescaped) {
                    html_unescape((char*)content, unescaped, xmlStrlen(content) + 1);
                    strncpy(entry->summary, unescaped, MAX_SUMMARY_LEN - 1);
                    free(unescaped);
                }
                xmlFree(content);
            }
        } else if (xmlStrcmp(node->name, (const xmlChar*)"link") == 0) {
            xmlChar* href = xmlGetProp(node, (const xmlChar*)"href");
            if (href) {
                strncpy(entry->url, (char*)href, MAX_URL_LEN - 1);
                xmlFree(href);
            }
        } else if (xmlStrcmp(node->name, (const xmlChar*)"published") == 0 ||
                   xmlStrcmp(node->name, (const xmlChar*)"updated") == 0) {
            content = get_node_content(node);
            if (content) {
                strncpy(entry->published, (char*)content, MAX_PUBLISHED_DATE - 1);
                xmlFree(content);
            }
        } else if (xmlStrcmp(node->name, (const xmlChar*)"author") == 0) {
            /* Look for name child */
            for (xmlNode* child = node->children; child; child = child->next) {
                if (xmlStrcmp(child->name, (const xmlChar*)"name") == 0) {
                    content = get_node_content(child);
                    if (content) {
                        strncpy(entry->author, (char*)content, sizeof(entry->author) - 1);
                        xmlFree(content);
                    }
                    break;
                }
            }
        }
    }
}

static void parse_rss_item(xmlNode* item_node, feed_entry_t* entry) {
    memset(entry, 0, sizeof(feed_entry_t));
    
    for (xmlNode* node = item_node->children; node; node = node->next) {
        xmlChar* content;
        
        if (xmlStrcmp(node->name, (const xmlChar*)"title") == 0) {
            content = get_node_content(node);
            if (content) {
                char* unescaped = malloc(xmlStrlen(content) + 1);
                if (unescaped) {
                    html_unescape((char*)content, unescaped, xmlStrlen(content) + 1);
                    strncpy(entry->title, unescaped, MAX_TITLE_LEN - 1);
                    free(unescaped);
                }
                xmlFree(content);
            }
        } else if (xmlStrcmp(node->name, (const xmlChar*)"description") == 0) {
            content = get_node_content(node);
            if (content) {
                char* unescaped = malloc(xmlStrlen(content) + 1);
                if (unescaped) {
                    html_unescape((char*)content, unescaped, xmlStrlen(content) + 1);
                    strncpy(entry->summary, unescaped, MAX_SUMMARY_LEN - 1);
                    free(unescaped);
                }
                xmlFree(content);
            }
        } else if (xmlStrcmp(node->name, (const xmlChar*)"link") == 0) {
            content = get_node_content(node);
            if (content) {
                strncpy(entry->url, (char*)content, MAX_URL_LEN - 1);
                xmlFree(content);
            }
        } else if (xmlStrcmp(node->name, (const xmlChar*)"pubDate") == 0 ||
                   xmlStrcmp(node->name, (const xmlChar*)"date") == 0) {
            content = get_node_content(node);
            if (content) {
                strncpy(entry->published, (char*)content, MAX_PUBLISHED_DATE - 1);
                xmlFree(content);
            }
        } else if (xmlStrcmp(node->name, (const xmlChar*)"author") == 0 ||
                   xmlStrcmp(node->name, (const xmlChar*)"creator") == 0) {
            content = get_node_content(node);
            if (content) {
                strncpy(entry->author, (char*)content, sizeof(entry->author) - 1);
                xmlFree(content);
            }
        }
    }
}

isae_error_t feed_parse_xml(const char* xml_content, size_t xml_size,
                            feed_result_t* result) {
    if (!xml_content || !result) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    (void)xml_size;  /* Content is null-terminated */
    
    /* Initialize libxml2 parser */
    xmlDocPtr doc = xmlReadMemory(xml_content, (int)strlen(xml_content), 
                                   NULL, NULL, XML_PARSE_RECOVER | XML_PARSE_NOERROR);
    if (!doc) {
        return ISAE_ERR_PARSE;
    }
    
    xmlNode* root = xmlDocGetRootElement(doc);
    if (!root) {
        xmlFreeDoc(doc);
        return ISAE_ERR_PARSE;
    }
    
    /* Allocate entries array */
    result->entries = calloc(MAX_ANNOUNCEMENTS_PER_FEED, sizeof(feed_entry_t));
    if (!result->entries) {
        xmlFreeDoc(doc);
        return ISAE_ERR_MEMORY;
    }
    
    /* Detect feed type and parse */
    int is_atom = (xmlStrcmp(root->name, (const xmlChar*)"feed") == 0);
    
    /* Get feed title */
    for (xmlNode* node = root->children; node; node = node->next) {
        if ((is_atom && xmlStrcmp(node->name, (const xmlChar*)"title") == 0) ||
            (!is_atom && xmlStrcmp(node->name, (const xmlChar*)"title") == 0)) {
            xmlChar* content = get_node_content(node);
            if (content) {
                strncpy(result->feed_title, (char*)content, sizeof(result->feed_title) - 1);
                xmlFree(content);
            }
            break;
        }
    }
    
    /* Parse entries/items */
    result->count = 0;
    for (xmlNode* node = root->children; node && result->count < MAX_ANNOUNCEMENTS_PER_FEED; 
         node = node->next) {
        
        if (is_atom && xmlStrcmp(node->name, (const xmlChar*)"entry") == 0) {
            parse_atom_entry(node, &result->entries[result->count]);
            if (result->entries[result->count].title[0] != '\0') {
                result->count++;
            }
        } else if (!is_atom && xmlStrcmp(node->name, (const xmlChar*)"item") == 0) {
            parse_rss_item(node, &result->entries[result->count]);
            if (result->entries[result->count].title[0] != '\0') {
                result->count++;
            }
        }
    }
    
    xmlFreeDoc(doc);
    return ISAE_OK;
}

isae_error_t feed_fetch(const char* feed_url, feed_result_t* result,
                        int timeout_seconds) {
    if (!feed_url || !result) {
        return ISAE_ERR_INVALID_PARAM;
    }
    
    feed_result_init(result);
    strncpy(result->feed_url, feed_url, MAX_URL_LEN - 1);
    
    /* Initialize libxml2 */
    xmlInitParser();
    
    /* Fetch feed content */
    http_response_t response;
    http_response_init(&response);
    
    http_client_config_t config;
    http_client_config_default(&config);
    config.timeout_seconds = timeout_seconds;
    
    isae_error_t err = http_get(feed_url, &response, &config);
    if (err != ISAE_OK) {
        http_response_cleanup(&response);
        xmlCleanupParser();
        return err;
    }
    
    /* Parse XML */
    if (response.body && response.body_size > 0) {
        err = feed_parse_xml(response.body, response.body_size, result);
    } else {
        err = ISAE_ERR_PARSE;
    }
    
    http_response_cleanup(&response);
    xmlCleanupParser();
    
    return err;
}
