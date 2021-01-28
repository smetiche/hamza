#include "hm-ot.h"
#include "hm-ot-shape-complex-arabic.h"

hm_feature_t
hm_ot_feature_from_tag(hm_tag tag) {
    /* loop over feature tag lookup table for it's id */
    int i;
    const hm_feature_info_t *feature_info = NULL;

    for (i = 0; i < HM_FEATURE_COUNT; ++i) {
        feature_info = &HM_FEATURE_INFO_LUT[i];
        if (feature_info->tag == tag) {
            return feature_info->feature;
        }
    }

    return -1;
}


hm_tag
hm_ot_tag_from_feature(hm_feature_t feature) {
    /* loop over feature tag lookup table for it's id */
    int i;
    const hm_feature_info_t *feature_info = NULL;

    for (i = 0; i < HM_FEATURE_COUNT; ++i) {
        feature_info = &HM_FEATURE_INFO_LUT[i];
        if (feature_info->feature == feature) {
            return feature_info->tag;
        }
    }

    return 0;
}











static void *
hm_ot_layout_choose_lang_sys(hm_face_t *face,
                             hm_byte *data,
                             hm_tag script,
                             hm_tag language) {
    hm_stream_t *subtable = hm_stream_create(data, 0, 0);
    uint16_t script_count = 0;
    uint16_t index = 0;

    hm_stream_read16(subtable, &script_count);
    HM_LOG("script count: %d\n", script_count);

    while (index < script_count) {
        hm_tag currTag;
        uint16_t currOffset;

        hm_stream_read32(subtable, &currTag);
        hm_stream_read16(subtable, &currOffset);

        HM_LOG("[%u] = \"%c%c%c%c\" (%u)\n", index, HM_UNTAG(currTag), currOffset);

        if (script == currTag) {
            /* Found script */
            uint16_t scriptOffset = currOffset;
            unsigned char *scriptData = data + scriptOffset;
            hm_stream_t *scriptStream = hm_stream_create(scriptData, 0, 0);
            hm_offset16 defaultLangSysOffset;
            uint16_t langSysCount;
            hm_stream_read16(scriptStream, &defaultLangSysOffset);
            hm_stream_read16(scriptStream, &langSysCount);

            HM_LOG("default lang sys: %u\n", defaultLangSysOffset);
            HM_LOG("lang sys count: %u\n", langSysCount);

            uint16_t langSysIndex = 0;
            while (langSysIndex < langSysCount) {
                hm_rec16_t langSysRec;
                hm_stream_read32(scriptStream, &langSysRec.tag);
                hm_stream_read16(scriptStream, &langSysRec.offset);

                HM_LOG("[%u] = \"%c%c%c%c\" %u\n", langSysIndex, HM_UNTAG(langSysRec.tag), langSysRec.offset);

                if (langSysRec.tag == language) {
                    /* Found language system */
                    return scriptData + langSysRec.offset;
                }

                ++langSysIndex;
            }

            /* Couldn't find alterior language system, return default. */
            return scriptData + defaultLangSysOffset;
        }

        ++index;
    }

    return NULL;
}

static void
hm_ot_layout_parse_lang_sys() {

}



void
hm_ot_layout_feature_get_lookups(uint8_t *data,
                                 hm_array_t *lookup_indices)
{

    hm_stream_t *table = hm_stream_create(data,0,0);
    hm_feature_table_t feature_table;
    hm_stream_read16(table, &feature_table.feature_params);
    hm_stream_read16(table, &feature_table.lookup_index_count);

//    HM_LOG("feature_params: 0x%04X\n", feature_table.feature_params);
//    HM_LOG("lookup_index_count: %u\n", feature_table.lookup_index_count);

    int i = 0;
    while (i < feature_table.lookup_index_count) {
        uint16_t lookup_index;
        hm_stream_read16(table, &lookup_index);
        hm_array_push_back(lookup_indices, lookup_index);
        ++i;
    }
}


hm_bool
hm_ot_layout_apply_features(hm_face_t *face,
                            hm_tag script,
                            hm_tag language,
                            const hm_bitset_t *feature_bits,
                            hm_array_t *glyph_array)
{
    HM_ASSERT(face != NULL);
    HM_ASSERT(feature_bits != NULL);

    hm_byte *data = (hm_byte *) face->gsub_table;
    hm_stream_t *table = hm_stream_create(data, 0, 0);
    uint32_t ver;
    uint16_t script_list_offset;
    uint16_t feature_list_offset;
    uint16_t lookup_list_offset;
    uint32_t feature_variations_offset;

    hm_stream_read32(table, &ver);

    HM_LOG("GSUB version: %u.%u\n", ver >> 16, ver & 0xFFFF);

    if (ver == 0x00010000) {
        hm_stream_read16(table, &script_list_offset);
        hm_stream_read16(table, &feature_list_offset);
        hm_stream_read16(table, &lookup_list_offset);
        HM_LOG("script_list_offset: %u\n", script_list_offset);
        HM_LOG("feature_list_offset: %u\n", feature_list_offset);
        HM_LOG("lookup_list_offset: %u\n", lookup_list_offset);
    }
    else if (ver == 0x00010001) {
        hm_stream_read16(table, &script_list_offset);
        hm_stream_read16(table, &feature_list_offset);
        hm_stream_read16(table, &lookup_list_offset);
        hm_stream_read32(table, &feature_variations_offset);
        HM_LOG("script_list_offset: %u\n", script_list_offset);
        HM_LOG("feature_list_offset: %u\n", feature_list_offset);
        HM_LOG("lookup_list_offset: %u\n", lookup_list_offset);
        HM_LOG("feature_variations_offset: %p\n", (void *) feature_variations_offset);
    }

    void *lsaddr = hm_ot_layout_choose_lang_sys(face,
                                                data + script_list_offset,
                                                script, language);

    if (lsaddr == NULL) {
        /* Language system was not found */
        HM_ERROR("Language system was not found!\n");
        return HM_FALSE;
    }

    HM_LOG("Found language system!\n");

    hm_array_t *lang_feature_indices = hm_array_create();
    hm_stream_t *lsbuf = hm_stream_create(lsaddr, 0, 0);

    hm_lang_sys_t langSys;
    hm_stream_read16(lsbuf, &langSys.lookupOrder);
    hm_stream_read16(lsbuf, &langSys.requiredFeatureIndex);
    hm_stream_read16(lsbuf, &langSys.featureIndexCount);

    /* lookupOrder should be (nil) */
    HM_LOG("lookupOrder: %p\n", (void *) langSys.lookupOrder);
    HM_LOG("requiredFeatureIndex: %u\n", langSys.requiredFeatureIndex);
    HM_LOG("featureIndexCount: %u\n", langSys.featureIndexCount);

    if (langSys.requiredFeatureIndex == 0xFFFF) {
        HM_LOG("No required features!\n");
    }

    uint16_t loopIndex = 0;
    while (loopIndex < langSys.featureIndexCount) {
        uint16_t featureIndex;
        hm_stream_read16(lsbuf, &featureIndex);
        HM_LOG("[%u] = %u\n", loopIndex, featureIndex);
        hm_array_push_back(lang_feature_indices, featureIndex);
        ++loopIndex;
    }

    hm_stream_t *lookup_list = hm_stream_create(data + lookup_list_offset, 0, 0);
    hm_array_t *lookup_offsets = hm_array_create();
    {
        /* Read lookup offets to table */
        uint16_t lookup_count;
        uint16_t lookup_index = 0;
        hm_stream_read16(lookup_list, &lookup_count);
        while (lookup_index < lookup_count) {
            uint16_t lookup_offset;
            hm_stream_read16(lookup_list, &lookup_offset);
            hm_array_push_back(lookup_offsets, lookup_offset);
            ++lookup_index;
        }
    }


    hm_stream_t *feature_list = hm_stream_create(data + feature_list_offset, 0, 0);


    {
        /* Parsing the FeatureList and applying selected Features */
        uint16_t feature_count;
        uint16_t feature_index = 0;
        hm_stream_read16(feature_list, &feature_count);
        HM_LOG("feature_count: %u\n", feature_count);

        while (feature_index < feature_count) {
            hm_rec16_t rec;
            hm_stream_read32(feature_list, &rec.tag);
            hm_stream_read16(feature_list, &rec.offset);
            HM_LOG("[%u] = { \"%c%c%c%c\", %u }\n", feature_index,
                     HM_UNTAG(rec.tag), rec.offset);

            hm_feature_t feature = hm_ot_feature_from_tag(rec.tag);

            if (hm_bitset_check(feature_bits, feature) == HM_TRUE) {
                /* Feature is requested and exists */
                hm_array_t *lookup_indices = hm_array_create();
                hm_ot_layout_feature_get_lookups(feature_list->data + rec.offset, lookup_indices);

                int i = 0;
                while (i < hm_array_size(lookup_indices)) {
                    uint16_t lookup_offset = hm_array_at(lookup_offsets, hm_array_at(lookup_indices, i));
                    hm_stream_t *lookup_table = hm_stream_create(lookup_list->data + lookup_offset, 0, 0);
                    hm_ot_layout_apply_lookup(face, lookup_table, feature, glyph_array);
                    ++i;
                }

                hm_array_destroy(lookup_indices);
            }

            ++feature_index;
        }
    }
}

void
hm_ot_layout_lookups_substitute_closure(hm_face_t *face,
                                          const hm_set_t *lookups,
                                          hm_set_t *glyphs)
{

}

hm_bool
hm_ot_layout_lookup_would_substitute(hm_face_t *face,
                                     unsigned int lookup_index,
                                     const hm_id *glyphs,
                                     unsigned int glyph_count,
                                     hm_bool zero_context)
{

}


hm_bool
hm_ot_layout_parse_coverage(uint8_t *data,
                            hm_array_t *coverage_glyphs)
{
    uint16_t coverage_format = 0;
    hm_stream_t *table = hm_stream_create(data,0,0);

    hm_stream_read16(table, &coverage_format);

    switch (coverage_format) {
        case 1: {
            uint16_t coverage_idx = 0;
            uint16_t coverage_glyph_count;
            hm_stream_read16(table, &coverage_glyph_count);
            while (coverage_idx < coverage_glyph_count) {
                uint16_t glyph_index;
                hm_stream_read16(table, &glyph_index);
                hm_array_push_back(coverage_glyphs, glyph_index);
                ++coverage_idx;
            }

            return HM_TRUE;
        }

        case 2: {
            uint16_t range_index = 0, range_count;
            hm_stream_read16(table, &range_count);

            /* Assuming ranges are ordered from 0 to glyph_count in order */
            while (range_index < range_count) {
                hm_id glyph_id;
                hm_range_rec_t range;

                hm_stream_read16(table, &range.start_glyph_id);
                hm_stream_read16(table, &range.end_glyph_id);
                hm_stream_read16(table, &range.start_coverage_index);

                glyph_id = range.start_glyph_id;
                while (glyph_id < range.end_glyph_id) {
                    hm_array_push_back(coverage_glyphs, glyph_id);
                    ++glyph_id;
                }

                ++range_index;
            }

            return HM_TRUE;
        }

        default: return HM_FALSE;
    }
}



void
hm_ot_layout_apply_lookup(hm_face_t *face,
                          hm_stream_t *table,
                          hm_feature_t feature,
                          hm_array_t *glyph_array)
{
    hm_lookup_table_t lookup;
    hm_stream_read16(table, &lookup.lookup_type);
    hm_stream_read16(table, &lookup.lookup_flag);
    hm_stream_read16(table, &lookup.subtable_count);

    HM_LOG("lookup_type: %d\n", lookup.lookup_type);
    HM_LOG("lookup_flag: %d\n", lookup.lookup_flag);
    HM_LOG("subtable_count: %d\n", lookup.subtable_count);

    uint16_t subtable_index = 0;
    while (subtable_index < lookup.subtable_count) {
        hm_offset16 offset;
        hm_stream_read16(table, &offset);
        hm_stream_t *subtable = hm_stream_create(table->data + offset, 0, 0);
        uint16_t subst_format;
        hm_stream_read16(subtable, &subst_format);

        switch (lookup.lookup_type) {
            case HM_GSUB_LOOKUP_TYPE_SINGLE_SUBSTITUTION: {
                if (subst_format == 1) {
                    hm_offset16 coverage_offset;
                    int16_t id_delta;
                    hm_stream_read16(subtable, &coverage_offset);
                    hm_stream_read16(subtable, (uint16_t *) &id_delta);
                    /* NOTE: Implement */
                }
                else if (subst_format == 2)
                {
                    hm_offset16 coverage_offset;
                    uint16_t glyph_count;
                    hm_array_t *from = hm_array_create();
                    hm_array_t *to = hm_array_create();
                    hm_stream_read16(subtable, &coverage_offset);
                    hm_stream_read16(subtable, &glyph_count);

                    /* Read coverage offset */
                    hm_ot_layout_parse_coverage(subtable->data + coverage_offset, from);

                    /* Get destination glyph indices */
                    uint16_t dst_gidx;
                    for (dst_gidx = 0; dst_gidx < glyph_count; ++dst_gidx) {
                        hm_id substitute_glyph;
                        hm_stream_read16(subtable, &substitute_glyph);
                        hm_array_push_back(to, substitute_glyph);
                    }

                    /* Substitute glyphs */
                    size_t gidx = 0;
                    while (gidx < hm_array_size(glyph_array)) {
                        hm_id curr_glyph = hm_array_at(glyph_array, gidx);
                        size_t val_idx;
                        if (hm_array_has(from, curr_glyph, &val_idx)) {

                            if (feature == HM_FEATURE_MEDI) {
                                if (hm_ot_shape_complex_arabic_medi_cond(face, glyph_array, gidx)) {
                                    hm_array_set(glyph_array, gidx, hm_array_at(to, val_idx));
                                }
                            } else if (feature == HM_FEATURE_INIT) {
                                if (hm_ot_shape_complex_arabic_init_cond(face, glyph_array, gidx)) {
                                    hm_array_set(glyph_array, gidx, hm_array_at(to, val_idx));
                                }
                            } else if (feature == HM_FEATURE_FINA) {
                                if (hm_ot_shape_complex_arabic_fina_cond(face, glyph_array, gidx)) {
                                    hm_array_set(glyph_array, gidx, hm_array_at(to, val_idx));
                                }
                            }
                            else {
                                hm_array_set(glyph_array, gidx, hm_array_at(to, val_idx));
                            }
                        }

                        ++gidx;
                    }

                    hm_array_destroy(from);
                    hm_array_destroy(to);
                }
                break;
            }

            case HM_GSUB_LOOKUP_TYPE_MULTIPLE_SUBSTITUTION: {
                break;
            }

            case HM_GSUB_LOOKUP_TYPE_ALTERNATE_SUBSTITUTION: {
                break;
            }

            case HM_GSUB_LOOKUP_TYPE_LIGATURE_SUBSTITUTION: {
                hm_offset16 coverage_offset;
                uint16_t ligature_set_count;
                hm_array_t *from;
                HM_ASSERT(subst_format == 1);

                from = hm_array_create();
                hm_stream_read16(subtable, &coverage_offset);
                hm_stream_read16(subtable, &ligature_set_count);

                /* Read coverage offset */
                hm_ot_layout_parse_coverage(subtable->data + coverage_offset, from);

                /* Substitute glyphs */
                int gcount = hm_array_size(glyph_array);
                int gidx = gcount - 1;
                while (gidx >= 0) {
                    hm_id curr_id = hm_array_at(glyph_array, gidx);
                    size_t val_idx;
                    if (hm_array_has(from, curr_id, &val_idx)) {
                        HM_LOG("curr_id: 0x%04x\n", curr_id);

                        uint16_t ligsetoff = bswap16(*(uint16_t *)(subtable->data + subtable->offset + val_idx * 2));
                        /* LigatureSet table (all ligatures beginning with the same glyph) */
                        hm_stream_t *ligature_set = hm_stream_create(subtable->data + ligsetoff,0,0);
                        uint16_t ligcount;
                        hm_stream_read16(ligature_set, &ligcount);
                        /* Check each ligature and if any matches replace */
                        uint16_t ligidx = 0;
                        while (ligidx < ligcount) {
                            hm_offset16 ligoff;
                            hm_stream_read16(ligature_set, &ligoff);
                            /* Ligature table */
                            hm_stream_t *ligature = hm_stream_create(ligature_set->data + ligoff,0,0);
                            uint16_t ligature_glyph;
                            uint16_t component_count;
                            hm_stream_read16(ligature, &ligature_glyph);
                            hm_stream_read16(ligature, &component_count);
                            uint16_t liglen = component_count + 1;

                            /* Check if ligature is applicable */
                            if (gcount - gidx >= liglen) {
//                                uint8_t tmpbuf[1024];
//                                hm_monotonic_alloc_t alloc = hm_monotonic_alloc_init(tmpbuf, 1024);
//
//                                hm_id *ligset = HM_MONOTONIC_ALLOC(uint16_t, component_count + 1);
                                hm_array_t *ligarr = hm_array_create();
                                hm_array_push_back(ligarr, curr_id);

                                /* Gather all components */
                                uint16_t component_index = 0;
                                while (component_index < component_count) {
                                    uint16_t glyph;
                                    hm_stream_read16(ligature, &glyph);
                                    hm_array_push_back(ligarr, glyph);
                                    ++component_index;
                                }

                                if (hm_array_range_eq(ligarr, 0, glyph_array, gidx, liglen)) {
                                    hm_array_pop_range_at(glyph_array, gidx, liglen);
                                    hm_array_insert(glyph_array, gidx, ligature_glyph);
                                    hm_array_destroy(ligarr);
                                    goto skip_done_lig;
                                }

                                hm_array_destroy(ligarr);
                            }

                            ++ligidx;
                        }
                    }

                    skip_done_lig:
                    --gidx;
                }

                hm_array_destroy(from);
                break;
            }

            case HM_GSUB_LOOKUP_TYPE_CONTEXTUAL_SUBSTITUTION: {
                break;
            }

            case HM_GSUB_LOOKUP_TYPE_CHAINED_CONTEXTS_SUBSTITUTION: {
                break;
            }

            case HM_GSUB_LOOKUP_TYPE_EXTENSION_SUBSTITUTION: {
                break;
            }

            case HM_GSUB_LOOKUP_TYPE_REVERSE_CHAINING_CONTEXTUAL_SINGLE_SUBSTITUTION: {
                break;
            }

            default:
                HM_LOG("Invalid GSUB lookup type!\n");
                break;
        }

        ++subtable_index;
    }
}

hm_tag
hm_ot_script_to_tag(hm_script_t script)
{
    switch (script) {
        case HM_SCRIPT_ARABIC: return HM_TAG('a','r','a','b');
        case HM_SCRIPT_LATIN: return HM_TAG('l','a','t','n');
    }

    return 0;
}

hm_tag
hm_ot_language_to_tag(hm_language_t language)
{
    switch (language) {
        case HM_LANGUAGE_ARABIC: return HM_TAG('A','R','A',' ');
        case HM_LANGUAGE_ENGLISH: return HM_TAG('E','N','G',' ');
        case HM_LANGUAGE_FRENCH: return HM_TAG('F','R','A',' ');
    }

    return 0;
}