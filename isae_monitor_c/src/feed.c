/**
 * @file feed.c
 * @brief Atom/RSS feed fetching and parsing implementation
 */

#include "isae_monitor/feed.h"
#include "isae_monitor/httpclient.h"
#include "isae_monitor/models.h"
#include "isae_monitor/config.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* Define _GNU_SOURCE for strdup */
#define _GNU_SOURCE 1

/* Forward declarations */
static void parse_atom_entry(xmlNodePtr node, announcement_t* ann);
static void parse_rss_entry(xmlNodePtr node, announcement_t* ann);
static char* get_node_content(xmlNodePtr node);
static time_t parse_date(const char* date_str);

void feed_result_init(feed_result_t* result) {
    if (!result) return;
    memset(result, 0, sizeof(feed_result_t));
    result->capacity = MAX_ANNOUNCEMENTS;
    result->announcements = calloc(result->capacity, sizeof(announcement_t));
    if (result->announcements) {
        for (size_t i = 0; i < result->capacity; i++) {
            announcement_init(&result->announcements[i]);
        }
    }
}

void feed_result_cleanup(feed_result_t* result) {
    if (!result) return;
    
    if (result->announcements) {
        for (size_t i = 0; i < result->count; i++) {
            announcement_cleanup(&result->announcements[i]);
        }
        free(result->announcements);
        result->announcements = NULL;
    }
    result->count = 0;
    result->capacity = 0;
}

const char* feed_error_string(feed_error_t error) {
    switch (error) {
        case FEED_OK: return "Success";
        case FEED_ERR_HTTP: return "HTTP error";
        case FEED_ERR_PARSE: return "Parse error";
        case FEED_ERR_EMPTY: return "Empty feed";
        default: return "Unknown error";
    }
}

static char* get_node_content(xmlNodePtr node) {
    if (!node) return NULL;
    
    xmlChar* content = xmlNodeGetContent(node);
    if (!content) return NULL;
    
    char* result = strdup((char*)content);
    xmlFree(content);
    return result;
}

static time_t parse_date(const char* date_str) {
    if (!date_str) return 0;
    
    struct tm tm_time = {0};
    time_t result = 0;
    
    /* Try ISO 8601 format */
    if (sscanf(date_str, "%d-%d-%dT%d:%d:%d", 
               &tm_time.tm_year, &tm_time.tm_mon, &tm_time.tm_mday,
               &tm_time.tm_hour, &tm_time.tm_min, &tm_time.tm_sec) >= 3) {
        tm_time.tm_year -= 1900;
        tm_time.tm_mon -= 1;
        result = mktime(&tm_time);
    } else if (sscanf(date_str, "%d-%d-%d", 
                      &tm_time.tm_year, &tm_time.tm_mon, &tm_time.tm_mday) == 3) {
        tm_time.tm_year -= 1900;
        tm_time.tm_mon -= 1;
        result = mktime(&tm_time);
    }
    
    return result;
}

static void parse_atom_entry(xmlNodePtr node, announcement_t* ann) {
    for (xmlNodePtr child = node->children; child; child = child->next) {
        if (child->type != XML_ELEMENT_NODE) continue;
        
        if (xmlStrcmp(child->name, (const xmlChar*)"title") == 0) {
            char* content = get_node_content(child);
            if (content) {
                strncpy(ann->title, content, MAX_ANNOUNCEMENT_TITLE - 1);
                ann->title[MAX_ANNOUNCEMENT_TITLE - 1] = '\0';
                free(content);
            }
        } else if (xmlStrcmp(child->name, (const xmlChar*)"link") == 0) {
            xmlChar* href = xmlGetProp(child, (const xmlChar*)"href");
            if (href) {
                strncpy(ann->link, (char*)href, MAX_ANNOUNCEMENT_LINK - 1);
                ann->link[MAX_ANNOUNCEMENT_LINK - 1] = '\0';
                xmlFree(href);
            }
        } else if (xmlStrcmp(child->name, (const xmlChar*)"summary") == 0 ||
                   xmlStrcmp(child->name, (const xmlChar*)"content") == 0) {
            char* content = get_node_content(child);
            if (content) {
                /* Store in summary field - will need to strip HTML later */
                strncpy(ann->summary, content, MAX_ANNOUNCEMENT_SUMMARY - 1);
                ann->summary[MAX_ANNOUNCEMENT_SUMMARY - 1] = '\0';
                free(content);
            }
        } else if (xmlStrcmp(child->name, (const xmlChar*)"published") == 0 ||
                   xmlStrcmp(child->name, (const xmlChar*)"updated") == 0) {
            char* content = get_node_content(child);
            if (content) {
                time_t ts = parse_date(content);
                if (ts > 0) {
                    strftime(ann->published, MAX_PUBLISHED_DATE, "%Y-%m-%dT%H:%M:%SZ", gmtime(&ts));
                }
                free(content);
            }
        }
    }
}

static void parse_rss_entry(xmlNodePtr node, announcement_t* ann) {
    for (xmlNodePtr child = node->children; child; child = child->next) {
        if (child->type != XML_ELEMENT_NODE) continue;
        
        if (xmlStrcmp(child->name, (const xmlChar*)"title") == 0) {
            char* content = get_node_content(child);
            if (content) {
                strncpy(ann->title, content, MAX_ANNOUNCEMENT_TITLE - 1);
                ann->title[MAX_ANNOUNCEMENT_TITLE - 1] = '\0';
                free(content);
            }
        } else if (xmlStrcmp(child->name, (const xmlChar*)"link") == 0) {
            char* content = get_node_content(child);
            if (content) {
                strncpy(ann->link, content, MAX_ANNOUNCEMENT_LINK - 1);
                ann->link[MAX_ANNOUNCEMENT_LINK - 1] = '\0';
                free(content);
            }
        } else if (xmlStrcmp(child->name, (const xmlChar*)"description") == 0) {
            char* content = get_node_content(child);
            if (content) {
                strncpy(ann->summary, content, MAX_ANNOUNCEMENT_SUMMARY - 1);
                ann->summary[MAX_ANNOUNCEMENT_SUMMARY - 1] = '\0';
                free(content);
            }
        } else if (xmlStrcmp(child->name, (const xmlChar*)"pubDate") == 0 ||
                   xmlStrcmp(child->name, (const xmlChar*)"date") == 0) {
            char* content = get_node_content(child);
            if (content) {
                time_t ts = parse_date(content);
                if (ts > 0) {
                    strftime(ann->published, MAX_PUBLISHED_DATE, "%Y-%m-%dT%H:%M:%SZ", gmtime(&ts));
                }
                free(content);
            }
        } else if (xmlStrcmp(child->name, (const xmlChar*)"guid") == 0 && strlen(ann->link) == 0) {
            char* content = get_node_content(child);
            if (content) {
                strncpy(ann->link, content, MAX_ANNOUNCEMENT_LINK - 1);
                ann->link[MAX_ANNOUNCEMENT_LINK - 1] = '\0';
                free(content);
            }
        }
    }
}

static int add_announcement(feed_result_t* result, const announcement_t* ann) {
    if (!result || !ann) return FEED_ERR_PARSE;
    
    if (result->count >= result->capacity) {
        size_t new_cap = result->capacity * 2;
        announcement_t* new_data = realloc(result->announcements, new_cap * sizeof(announcement_t));
        if (!new_data) return FEED_ERR_PARSE;
        
        result->announcements = new_data;
        result->capacity = new_cap;
        
        for (size_t i = result->count; i < result->capacity; i++) {
            announcement_init(&result->announcements[i]);
        }
    }
    
    announcement_t* dest = &result->announcements[result->count];
    announcement_cleanup(dest);
    memcpy(dest, ann, sizeof(announcement_t));
    
    /* Ensure strings are properly copied */
    dest->title[MAX_ANNOUNCEMENT_TITLE - 1] = '\0';
    dest->link[MAX_ANNOUNCEMENT_LINK - 1] = '\0';
    dest->published[MAX_PUBLISHED_DATE - 1] = '\0';
    dest->summary[MAX_ANNOUNCEMENT_SUMMARY - 1] = '\0';
    dest->category[MAX_DEPARTMENT_KEY - 1] = '\0';
    
    result->count++;
    return FEED_OK;
}

feed_error_t feed_fetch(const settings_t* settings, feed_result_t* result) {
    if (!settings || !result) return FEED_ERR_PARSE;
    
    const char* feed_url = settings->feed_url;
    if (!feed_url || strlen(feed_url) == 0) {
        return FEED_ERR_EMPTY;
    }
    
    http_response_t response = {0};
    http_response_init(&response);
    
    isae_error_t ret = http_request(feed_url, "GET", NULL, NULL, 
                                     settings->request_timeout, settings->max_retries, &response);
    if (ret != ISAE_OK) {
        http_response_cleanup(&response);
        return FEED_ERR_HTTP;
    }
    
    if (response.body_size > 0 && response.body[response.body_size - 1] != '\0') {
        char* new_body = realloc(response.body, response.body_size + 1);
        if (new_body) {
            response.body = new_body;
            response.body[response.body_size] = '\0';
        }
    }
    
    xmlDocPtr doc = xmlReadMemory(response.body, strlen(response.body), NULL, NULL, 
                                   XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    http_response_cleanup(&response);
    
    if (!doc) {
        return FEED_ERR_PARSE;
    }
    
    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!root) {
        xmlFreeDoc(doc);
        return FEED_ERR_PARSE;
    }
    
    if (xmlStrcmp(root->name, (const xmlChar*)"feed") == 0) {
        /* Atom feed */
        for (xmlNodePtr node = root->children; node; node = node->next) {
            if (node->type == XML_ELEMENT_NODE &&
                xmlStrcmp(node->name, (const xmlChar*)"entry") == 0) {
                
                announcement_t ann = {0};
                announcement_init(&ann);
                parse_atom_entry(node, &ann);
                
                if (strlen(ann.title) > 0 && strlen(ann.link) > 0) {
                    ret = add_announcement(result, &ann);
                    if (ret < 0) {
                        announcement_cleanup(&ann);
                        break;
                    }
                } else {
                    announcement_cleanup(&ann);
                }
            }
        }
    } else if (xmlStrcmp(root->name, (const xmlChar*)"rss") == 0 ||
               xmlStrcmp(root->name, (const xmlChar*)"channel") == 0) {
        /* RSS feed */
        xmlNodePtr channel = root;
        if (xmlStrcmp(root->name, (const xmlChar*)"rss") == 0) {
            for (xmlNodePtr node = root->children; node; node = node->next) {
                if (node->type == XML_ELEMENT_NODE && 
                    xmlStrcmp(node->name, (const xmlChar*)"channel") == 0) {
                    channel = node;
                    break;
                }
            }
        }
        
        for (xmlNodePtr node = channel->children; node; node = node->next) {
            if (node->type == XML_ELEMENT_NODE &&
                xmlStrcmp(node->name, (const xmlChar*)"item") == 0) {
                
                announcement_t ann = {0};
                announcement_init(&ann);
                parse_rss_entry(node, &ann);
                
                if (strlen(ann.title) > 0 && strlen(ann.link) > 0) {
                    ret = add_announcement(result, &ann);
                    if (ret < 0) {
                        announcement_cleanup(&ann);
                        break;
                    }
                } else {
                    announcement_cleanup(&ann);
                }
            }
        }
    }
    
    xmlFreeDoc(doc);
    
    if (result->count == 0) {
        return FEED_ERR_EMPTY;
    }
    
    return FEED_OK;
}
