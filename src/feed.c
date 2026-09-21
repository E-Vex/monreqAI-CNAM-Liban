/**
 * ISAE Monitor - Feed Parser
 *
 * Faithful C port of Python isae_monitor/feed.py.
 *
 * Behavior preserved:
 *   - Atom entries are parsed via libxml2 (substitutes for feedparser).
 *   - The Atom <id> is the dedup key; falls back to <link rel=alternate>
 *     if missing.
 *   - <link rel="alternate"> is preferred over <link rel="self"> etc.
 *   - Feed is reversed to oldest-first (Blogger returns newest-first).
 *   - Entries with empty IDs are dropped.
 *   - Title defaults to "(sans titre)" when missing.
 *   - Published defaults to "date inconnue" when missing.
 *   - Summary is HTML-stripped (strip_html) and truncated to 500 chars
 *     at the feed-parsing stage (mirrors Python's feed-side cap).
 *   - HTML entities are unescaped ONCE (libxml2 already unescapes XML
 *     entities; the previous C code re-unescaped and produced wrong
 *     output for "&amp;amp;" inputs).
 *   - xmlInitParser is called once per process (idempotent in libxml2);
 *   - xmlCleanupParser is NOT called per fetch (the docs warn it can
 *     double-free global state when called repeatedly).
 */

#include "isae_monitor/feed.h"
#include "isae_monitor/httpclient.h"
#include "isae_monitor/models.h"

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SUMMARY_FEED_LIMIT 500

void feed_result_init(feed_result_t* result) {
    if (!result) return;
    result->entries = NULL;
    result->count = 0;
    result->feed_title[0] = '\0';
}

void feed_result_cleanup(feed_result_t* result) {
    if (!result) return;
    free(result->entries);
    result->entries = NULL;
    result->count = 0;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Copy xmlChar* content (already entity-unescaped by libxml2) into a
 * fixed-size C string, NUL-terminating. */
static void copy_xml_field(char* dst, size_t dst_size, const xmlChar* src) {
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t n = xmlStrlen(src);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool is_blank(const char* s) {
    if (!s) return true;
    while (*s) {
        if (*s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') return false;
        s++;
    }
    return true;
}

/* Truncate a UTF-8 string at the codepoint boundary so the byte length
 * is <= max_bytes. Always NUL-terminates. */
static void truncate_utf8(char* s, size_t max_bytes) {
    if (!s) return;
    size_t len = strlen(s);
    if (len <= max_bytes) return;
    /* Walk back from max_bytes until we're not in the middle of a
     * multibyte sequence (a continuation byte has top bits 10xxxxxx). */
    size_t cut = max_bytes;
    while (cut > 0 && (unsigned char)s[cut - 1] >= 0x80 && (unsigned char)s[cut - 1] < 0xC0) {
        cut--;
    }
    s[cut] = '\0';
}

/* Read the value of <link rel="alternate" href="...">. If none, take
 * the first <link> with an href. Atom entries often have multiple
 * <link> elements; we want the one a user would click. */
static void read_atom_link(xmlNode* entry_node, char* link_out, size_t link_size) {
    link_out[0] = '\0';
    char fallback[MAX_URL_LEN] = "";

    for (xmlNode* n = entry_node->children; n; n = n->next) {
        if (xmlStrcmp(n->name, (const xmlChar*)"link") != 0) continue;
        xmlChar* href = xmlGetProp(n, (const xmlChar*)"href");
        if (!href) continue;
        xmlChar* rel = xmlGetProp(n, (const xmlChar*)"rel");
        bool is_alternate = (!rel || xmlStrcmp(rel, (const xmlChar*)"alternate") == 0);
        if (is_alternate) {
            copy_xml_field(link_out, link_size, href);
            xmlFree(href);
            if (rel) xmlFree(rel);
            return;
        }
        if (!fallback[0]) copy_xml_field(fallback, sizeof(fallback), href);
        xmlFree(href);
        if (rel) xmlFree(rel);
    }
    if (!link_out[0] && fallback[0]) {
        copy_xml_field(link_out, link_size, (const xmlChar*)fallback);
    }
}

/* ------------------------------------------------------------------ */
/* Atom entry parsing                                                  */
/* ------------------------------------------------------------------ */

static void parse_atom_entry(xmlNode* entry_node, feed_entry_t* entry) {
    memset(entry, 0, sizeof(*entry));

    for (xmlNode* node = entry_node->children; node; node = node->next) {
        if (xmlStrcmp(node->name, (const xmlChar*)"title") == 0) {
            xmlChar* content = xmlNodeGetContent(node);
            if (content) {
                copy_xml_field(entry->title, sizeof(entry->title), content);
                xmlFree(content);
            }
        } else if (xmlStrcmp(node->name, (const xmlChar*)"id") == 0) {
            xmlChar* content = xmlNodeGetContent(node);
            if (content) {
                copy_xml_field(entry->id, sizeof(entry->id), content);
                xmlFree(content);
            }
        } else if (xmlStrcmp(node->name, (const xmlChar*)"summary") == 0 ||
                   xmlStrcmp(node->name, (const xmlChar*)"content") == 0) {
            xmlChar* content = xmlNodeGetContent(node);
            if (content) {
                /* strip_html unescapes entities and removes tags. libxml2
                 * already unescaped once, so we don't double-unescape here. */
                char stripped[MAX_SUMMARY_LEN];
                strip_html((const char*)content, stripped, sizeof(stripped));
                truncate_utf8(stripped, SUMMARY_FEED_LIMIT);
                copy_xml_field(entry->summary, sizeof(entry->summary), (const xmlChar*)stripped);
                xmlFree(content);
            }
        } else if (xmlStrcmp(node->name, (const xmlChar*)"link") == 0) {
            /* Handled below by read_atom_link() so we can pick rel=alternate. */
        } else if (xmlStrcmp(node->name, (const xmlChar*)"published") == 0 ||
                   xmlStrcmp(node->name, (const xmlChar*)"updated") == 0) {
            if (entry->published[0]) continue;  /* first one wins */
            xmlChar* content = xmlNodeGetContent(node);
            if (content) {
                copy_xml_field(entry->published, sizeof(entry->published), content);
                xmlFree(content);
            }
        }
    }

    read_atom_link(entry_node, entry->link, sizeof(entry->link));

    /* Fallbacks matching Python's Announcement.from_entry. */
    if (is_blank(entry->title)) {
        strncpy(entry->title, "(sans titre)", sizeof(entry->title) - 1);
        entry->title[sizeof(entry->title) - 1] = '\0';
    }
    if (is_blank(entry->published)) {
        strncpy(entry->published, "date inconnue", sizeof(entry->published) - 1);
        entry->published[sizeof(entry->published) - 1] = '\0';
    }
    if (is_blank(entry->id)) {
        /* Python falls back to entry.get('link', '') or "". */
        copy_xml_field(entry->id, sizeof(entry->id), (const xmlChar*)entry->link);
    }
}

/* ------------------------------------------------------------------ */
/* RSS item parsing (kept for parity; the ISAE feed is Atom)          */
/* ------------------------------------------------------------------ */

static void parse_rss_item(xmlNode* item_node, feed_entry_t* entry) {
    memset(entry, 0, sizeof(*entry));

    for (xmlNode* node = item_node->children; node; node = node->next) {
        if (xmlStrcmp(node->name, (const xmlChar*)"title") == 0) {
            xmlChar* content = xmlNodeGetContent(node);
            if (content) {
                copy_xml_field(entry->title, sizeof(entry->title), content);
                xmlFree(content);
            }
        } else if (xmlStrcmp(node->name, (const xmlChar*)"guid") == 0) {
            xmlChar* content = xmlNodeGetContent(node);
            if (content) {
                copy_xml_field(entry->id, sizeof(entry->id), content);
                xmlFree(content);
            }
        } else if (xmlStrcmp(node->name, (const xmlChar*)"description") == 0) {
            xmlChar* content = xmlNodeGetContent(node);
            if (content) {
                char stripped[MAX_SUMMARY_LEN];
                strip_html((const char*)content, stripped, sizeof(stripped));
                truncate_utf8(stripped, SUMMARY_FEED_LIMIT);
                copy_xml_field(entry->summary, sizeof(entry->summary), (const xmlChar*)stripped);
                xmlFree(content);
            }
        } else if (xmlStrcmp(node->name, (const xmlChar*)"link") == 0) {
            xmlChar* content = xmlNodeGetContent(node);
            if (content) {
                copy_xml_field(entry->link, sizeof(entry->link), content);
                xmlFree(content);
            }
        } else if (xmlStrcmp(node->name, (const xmlChar*)"pubDate") == 0 ||
                   xmlStrcmp(node->name, (const xmlChar*)"date") == 0) {
            xmlChar* content = xmlNodeGetContent(node);
            if (content) {
                copy_xml_field(entry->published, sizeof(entry->published), content);
                xmlFree(content);
            }
        }
    }

    if (is_blank(entry->title)) {
        strncpy(entry->title, "(sans titre)", sizeof(entry->title) - 1);
        entry->title[sizeof(entry->title) - 1] = '\0';
    }
    if (is_blank(entry->published)) {
        strncpy(entry->published, "date inconnue", sizeof(entry->published) - 1);
        entry->published[sizeof(entry->published) - 1] = '\0';
    }
    if (is_blank(entry->id)) {
        copy_xml_field(entry->id, sizeof(entry->id), (const xmlChar*)entry->link);
    }
}

/* ------------------------------------------------------------------ */
/* XML parser                                                          */
/* ------------------------------------------------------------------ */

isae_error_t feed_parse_xml(const char* xml_content, size_t xml_size,
                            feed_result_t* result) {
    if (!xml_content || !result) return ISAE_ERR_INVALID_PARAM;
    feed_result_init(result);

    size_t real_size = xml_size > 0 ? xml_size : strlen(xml_content);
    if (real_size == 0) return ISAE_ERR_PARSE;

    xmlDocPtr doc = xmlReadMemory(xml_content, (int)real_size, NULL, NULL,
                                  XML_PARSE_RECOVER | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc) return ISAE_ERR_PARSE;

    xmlNode* root = xmlDocGetRootElement(doc);
    if (!root) { xmlFreeDoc(doc); return ISAE_ERR_PARSE; }

    bool is_atom = (xmlStrcmp(root->name, (const xmlChar*)"feed") == 0);

    result->entries = (feed_entry_t*)calloc(MAX_ANNOUNCEMENTS_PER_FEED, sizeof(feed_entry_t));
    if (!result->entries) { xmlFreeDoc(doc); return ISAE_ERR_MEMORY; }

    /* Read feed-level title. */
    for (xmlNode* node = root->children; node; node = node->next) {
        if (xmlStrcmp(node->name, (const xmlChar*)"title") == 0) {
            xmlChar* content = xmlNodeGetContent(node);
            if (content) {
                copy_xml_field(result->feed_title, sizeof(result->feed_title), content);
                xmlFree(content);
            }
            break;
        }
    }

    /* For RSS, items live inside <channel>. */
    xmlNode* container = root;
    if (!is_atom) {
        for (xmlNode* node = root->children; node; node = node->next) {
            if (xmlStrcmp(node->name, (const xmlChar*)"channel") == 0) {
                container = node;
                break;
            }
        }
    }

    for (xmlNode* node = container->children;
         node && result->count < MAX_ANNOUNCEMENTS_PER_FEED;
         node = node->next) {
        if (is_atom && xmlStrcmp(node->name, (const xmlChar*)"entry") == 0) {
            parse_atom_entry(node, &result->entries[result->count]);
        } else if (!is_atom && xmlStrcmp(node->name, (const xmlChar*)"item") == 0) {
            parse_rss_item(node, &result->entries[result->count]);
        } else {
            continue;
        }
        /* Drop entries with an empty id (mirrors Python's "if a.id"
         * filter after the fallback-to-link attempt). */
        if (result->entries[result->count].id[0]) {
            result->count++;
        }
    }

    xmlFreeDoc(doc);

    /* Reverse to oldest-first (Blogger returns newest-first). */
    for (size_t i = 0, j = result->count - 1; i < j; i++, j--) {
        feed_entry_t tmp = result->entries[i];
        result->entries[i] = result->entries[j];
        result->entries[j] = tmp;
    }

    return ISAE_OK;
}

/* ------------------------------------------------------------------ */
/* HTTP fetch                                                          */
/* ------------------------------------------------------------------ */

isae_error_t feed_fetch(const char* feed_url,
                        const http_client_config_t* http_cfg,
                        feed_result_t* result) {
    if (!feed_url || !result) return ISAE_ERR_INVALID_PARAM;
    feed_result_init(result);

    /* libxml2 init is idempotent; cleanup is the program's responsibility
     * (calling xmlCleanupParser per fetch is unsafe -- it can double-free
     * libxml2's global state). */
    xmlInitParser();

    http_client_config_t defaults;
    if (!http_cfg) {
        http_client_config_default(&defaults);
        http_cfg = &defaults;
    }

    http_response_t response;
    http_response_init(&response);
    isae_error_t err = http_get(feed_url, &response, http_cfg);
    if (err != ISAE_OK) {
        http_response_cleanup(&response);
        return ISAE_ERR_FEED;
    }

    if (!response.body || response.body_size == 0) {
        http_response_cleanup(&response);
        return ISAE_ERR_FEED;
    }

    err = feed_parse_xml(response.body, response.body_size, result);
    http_response_cleanup(&response);
    if (err != ISAE_OK && result->count == 0) {
        return ISAE_ERR_FEED;
    }
    return ISAE_OK;
}

/* ------------------------------------------------------------------ */
/* feed_entry_t -> announcement_t                                      */
/* ------------------------------------------------------------------ */

isae_error_t feed_entry_to_announcement(const feed_entry_t* entry,
                                         announcement_t* ann) {
    if (!entry || !ann) return ISAE_ERR_INVALID_PARAM;
    announcement_init(ann);

#define DUP(field) do { \
    if (entry->field[0]) { \
        ann->field = strdup(entry->field); \
        if (!ann->field) { announcement_cleanup(ann); return ISAE_ERR_MEMORY; } \
    } \
} while (0)
    DUP(id);
    DUP(title);
    DUP(link);
    DUP(published);
    DUP(summary);
#undef DUP
    return ISAE_OK;
}
