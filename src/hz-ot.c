#include "hz-ot.h"
#include "hz-ot-shape-complex-arabic.h"
#include "util/hz-map.h"

hz_feature_t
hz_ot_feature_from_tag(hz_tag tag) {
    /* loop over feature tag lookup table for it's id */
    int i;
    const hz_feature_info_t *feature_info = NULL;

    for (i = 0; i < HZ_FEATURE_COUNT; ++i) {
        feature_info = &HZ_FEATURE_INFO_LUT[i];
        if (feature_info->tag == tag) {
            return feature_info->feature;
        }
    }

    return -1;
}

hz_tag
hz_ot_tag_from_feature(hz_feature_t feature) {
    /* loop over feature tag lookup table for it's id */
    int i;
    const hz_feature_info_t *feature_info = NULL;

    for (i = 0; i < HZ_FEATURE_COUNT; ++i) {
        feature_info = &HZ_FEATURE_INFO_LUT[i];
        if (feature_info->feature == feature) {
            return feature_info->tag;
        }
    }

    return 0;
}











static void *
hz_ot_layout_choose_lang_sys(hz_face_t *face,
                             hz_byte *data,
                             hz_tag script,
                             hz_tag language)
 {
    hz_byte tmpbuf[1024];
    cmas_mono_ma_t ma = cmas_mono_ma_create(tmpbuf, 1024);

    hz_stream_t *subtable = hz_stream_create(data, 0, 0);
    uint16_t script_count = 0;
    uint16_t index = 0;
    hz_rec16_t *script_records = NULL;
    uint16_t found_script = 0;

    hz_stream_read16(subtable, &script_count);
    HZ_LOG("script count: %d\n", script_count);
    script_records = cmas_mono_ma_alloc(&ma, sizeof(hz_rec16_t) * script_count);

    while (index < script_count) {
        hz_tag curr_tag;
        uint16_t curr_offset;

        hz_stream_read32(subtable, &curr_tag);
        hz_stream_read16(subtable, &curr_offset);

        HZ_LOG("[%u] = \"%c%c%c%c\" (%u)\n", index, HZ_UNTAG(curr_tag), curr_offset);

        script_records[index].offset = curr_offset;
        script_records[index].tag = curr_tag;

        if (script == curr_tag) {
            found_script = index;
            break;
        }

        ++index;
    }

    /* Found script */
    uint16_t script_offset = script_records[found_script].offset;
    hz_stream_t *script_stream = hz_stream_create(data + script_offset, 0, 0);
    hz_offset16 default_lang_sys_offset;
    uint16_t lang_sys_count;
    hz_stream_read16(script_stream, &default_lang_sys_offset);
    hz_stream_read16(script_stream, &lang_sys_count);

    HZ_LOG("default lang sys: %u\n", default_lang_sys_offset);
    HZ_LOG("lang sys count: %u\n", lang_sys_count);

    uint16_t langSysIndex = 0;
    while (langSysIndex < lang_sys_count) {
        hz_rec16_t lang_sys_rec;
        hz_stream_read32(script_stream, &lang_sys_rec.tag);
        hz_stream_read16(script_stream, &lang_sys_rec.offset);

        HZ_LOG("[%u] = \"%c%c%c%c\" %u\n", langSysIndex, HZ_UNTAG(lang_sys_rec.tag), lang_sys_rec.offset);

        if (lang_sys_rec.tag == language) {
            /* Found language system */
            return script_stream->data + lang_sys_rec.offset;
        }

        ++langSysIndex;
    }

    /* Couldn't find alterior language system, return default. */
    return script_stream->data + default_lang_sys_offset;
}

static void
hz_ot_layout_parse_lang_sys() {

}



void
hz_ot_layout_feature_get_lookups(const uint8_t *data,
                                 hz_array_t *lookup_indices)
{

    hz_stream_t *table = hz_stream_create(data,0,0);
    hz_feature_table_t feature_table;
    hz_stream_read16(table, &feature_table.feature_params);
    hz_stream_read16(table, &feature_table.lookup_index_count);

//    HZ_LOG("feature_params: 0x%04X\n", feature_table.feature_params);
//    HZ_LOG("lookup_index_count: %u\n", feature_table.lookup_index_count);

    int i = 0;
    while (i < feature_table.lookup_index_count) {
        uint16_t lookup_index;
        hz_stream_read16(table, &lookup_index);
        hz_array_push_back(lookup_indices, lookup_index);
        ++i;
    }
}


hz_bool
hz_ot_layout_apply_gsub_features(hz_face_t *face,
                                 hz_tag script,
                                 hz_tag language,
                                 const hz_array_t *wanted_features,
                                 hz_section_t *sect)
{
    HZ_ASSERT(face != NULL);
    HZ_ASSERT(wanted_features != NULL);

    hz_byte *data = (hz_byte *) face->gsub_table;
    hz_stream_t *table = hz_stream_create(data, 0, 0);
    HZ_Version16Dot16 ver;
    uint16_t script_list_offset;
    uint16_t feature_list_offset;
    uint16_t lookup_list_offset;
    uint32_t feature_variations_offset;

    hz_stream_read32(table, &ver);

    HZ_LOG("GSUB version: %u.%u\n", ver >> 16, ver & 0xFFFF);

    if (ver == 0x00010000) {
        hz_stream_read16(table, &script_list_offset);
        hz_stream_read16(table, &feature_list_offset);
        hz_stream_read16(table, &lookup_list_offset);
        HZ_LOG("script_list_offset: %u\n", script_list_offset);
        HZ_LOG("feature_list_offset: %u\n", feature_list_offset);
        HZ_LOG("lookup_list_offset: %u\n", lookup_list_offset);
    }
    else if (ver == 0x00010001) {
        hz_stream_read16(table, &script_list_offset);
        hz_stream_read16(table, &feature_list_offset);
        hz_stream_read16(table, &lookup_list_offset);
        hz_stream_read32(table, &feature_variations_offset);
        HZ_LOG("script_list_offset: %u\n", script_list_offset);
        HZ_LOG("feature_list_offset: %u\n", feature_list_offset);
        HZ_LOG("lookup_list_offset: %u\n", lookup_list_offset);
        HZ_LOG("feature_variations_offset: %p\n", (void *) feature_variations_offset);
    }

    void *lsaddr = hz_ot_layout_choose_lang_sys(face,
                                                data + script_list_offset,
                                                script, language);

    if (lsaddr == NULL) {
        /* Language system was not found */
        HZ_ERROR("Language system was not found!\n");
        return HZ_FALSE;
    }

    HZ_LOG("Found language system!\n");

    hz_array_t *lang_feature_indices = hz_array_create();
    hz_stream_t *lsbuf = hz_stream_create(lsaddr, 0, 0);

    hz_lang_sys_t langSys;
    hz_stream_read16(lsbuf, &langSys.lookupOrder);
    hz_stream_read16(lsbuf, &langSys.requiredFeatureIndex);
    hz_stream_read16(lsbuf, &langSys.featureIndexCount);

    /* lookupOrder should be (nil) */
    HZ_LOG("lookupOrder: %p\n", (void *) langSys.lookupOrder);
    HZ_LOG("requiredFeatureIndex: %u\n", langSys.requiredFeatureIndex);
    HZ_LOG("featureIndexCount: %u\n", langSys.featureIndexCount);

    if (langSys.requiredFeatureIndex == 0xFFFF) {
        HZ_LOG("No required features!\n");
    }

    uint16_t loopIndex = 0;
    while (loopIndex < langSys.featureIndexCount) {
        uint16_t featureIndex;
        hz_stream_read16(lsbuf, &featureIndex);
        HZ_LOG("[%u] = %u\n", loopIndex, featureIndex);
        hz_array_push_back(lang_feature_indices, featureIndex);
        ++loopIndex;
    }

    hz_stream_t *lookup_list = hz_stream_create(data + lookup_list_offset, 0, 0);
    hz_array_t *lookup_offsets = hz_array_create();
    {
        /* Read lookup offets to table */
        uint16_t lookup_count;
        uint16_t lookup_index = 0;
        hz_stream_read16(lookup_list, &lookup_count);
        while (lookup_index < lookup_count) {
            uint16_t lookup_offset;
            hz_stream_read16(lookup_list, &lookup_offset);
            hz_array_push_back(lookup_offsets, lookup_offset);
            ++lookup_index;
        }
    }


    hz_stream_t *feature_list = hz_stream_create(data + feature_list_offset, 0, 0);


    {
        /* Parsing the FeatureList and applying selected Features */
        uint16_t feature_count;
        uint16_t feature_index = 0;
        hz_stream_read16(feature_list, &feature_count);
        HZ_LOG("feature_count: %u\n", feature_count);

        hz_map_t *feature_map = hz_map_create();

        /* fill map from feature type to offset */
        while (feature_index < feature_count) {
            hz_tag tag;
            uint16_t offset;
            hz_stream_read32(feature_list, &tag);
            hz_stream_read16(feature_list, &offset);
            hz_feature_t feature = hz_ot_feature_from_tag(tag);
            hz_map_set_value(feature_map, feature, offset);
            ++feature_index;
        }

        uint16_t wanted_feature_count = hz_array_size(wanted_features);
        uint16_t wanted_feature_index = 0;
        while (wanted_feature_index < wanted_feature_count) {
            hz_feature_t wanted_feature = hz_array_at(wanted_features, wanted_feature_index);

            if ( hz_map_value_exists(feature_map, wanted_feature) ) {
                /* feature is wanted and exists */
                hz_offset16 feature_offset = hz_map_get_value(feature_map, wanted_feature);
                hz_array_t *lookup_indices = hz_array_create();
                hz_ot_layout_feature_get_lookups(feature_list->data + feature_offset, lookup_indices);

                int i = 0;
                while (i < hz_array_size(lookup_indices)) {
                    uint16_t lookup_offset = hz_array_at(lookup_offsets, hz_array_at(lookup_indices, i));
                    hz_stream_t *lookup_table = hz_stream_create(lookup_list->data + lookup_offset, 0, 0);
                    hz_ot_layout_apply_gsub_lookup(face, lookup_table, wanted_feature, sect);
                    ++i;
                }

                hz_array_destroy(lookup_indices);
            }

            ++wanted_feature_index;
        }


        hz_map_destroy(feature_map);
    }

    return HZ_TRUE;
}

hz_bool
hz_ot_layout_apply_gpos_features(hz_face_t *face,
                                 hz_tag script,
                                 hz_tag language,
                                 const hz_array_t *wanted_features,
                                 hz_section_t *sect)
{
    HZ_ASSERT(face != NULL);
    HZ_ASSERT(wanted_features != NULL);

    hz_byte *data = (hz_byte *) face->gpos_table;
    hz_stream_t *table = hz_stream_create(data, 0, 0);
    HZ_Version16Dot16 ver;
    uint16_t script_list_offset;
    uint16_t feature_list_offset;
    uint16_t lookup_list_offset;
    uint32_t feature_variations_offset;

    hz_stream_read32(table, &ver);
    HZ_LOG("GPOS version: %u.%u\n", ver >> 16, ver & 0xFFFF);

    switch (ver) {
        case 0x00010000: /* 1.0 */
            hz_stream_read16(table, &script_list_offset);
            hz_stream_read16(table, &feature_list_offset);
            hz_stream_read16(table, &lookup_list_offset);
            break;
        case 0x00010001: /* 1.1 */
            hz_stream_read16(table, &script_list_offset);
            hz_stream_read16(table, &feature_list_offset);
            hz_stream_read16(table, &lookup_list_offset);
            hz_stream_read32(table, &feature_variations_offset);
            break;
        default: /* error */
            break;
    }

    void *lsaddr = hz_ot_layout_choose_lang_sys(face,
                                                table->data + script_list_offset,
                                                script, language);

    if (lsaddr == NULL) {
        /* Language system was not found */
        HZ_ERROR("Language system was not found!\n");
        return HZ_FALSE;
    }

    HZ_LOG("Found language system!\n");

    hz_array_t *lang_feature_indices = hz_array_create();
    hz_stream_t *lsbuf = hz_stream_create(lsaddr, 0, 0);

    hz_lang_sys_t langSys;
    hz_stream_read16(lsbuf, &langSys.lookupOrder);
    hz_stream_read16(lsbuf, &langSys.requiredFeatureIndex);
    hz_stream_read16(lsbuf, &langSys.featureIndexCount);

    /* lookupOrder should be (nil) */
    HZ_LOG("lookupOrder: %p\n", (void *) langSys.lookupOrder);
    HZ_LOG("requiredFeatureIndex: %u\n", langSys.requiredFeatureIndex);
    HZ_LOG("featureIndexCount: %u\n", langSys.featureIndexCount);

    if (langSys.requiredFeatureIndex == 0xFFFF) {
        HZ_LOG("No required features!\n");
    }

    uint16_t loopIndex = 0;
    while (loopIndex < langSys.featureIndexCount) {
        uint16_t featureIndex;
        hz_stream_read16(lsbuf, &featureIndex);
        HZ_LOG("[%u] = %u\n", loopIndex, featureIndex);
        hz_array_push_back(lang_feature_indices, featureIndex);
        ++loopIndex;
    }

    hz_stream_t *lookup_list = hz_stream_create(data + lookup_list_offset, 0, 0);
    hz_array_t *lookup_offsets = hz_array_create();
    {
        /* Read lookup offets to table */
        uint16_t lookup_count;
        uint16_t lookup_index = 0;
        hz_stream_read16(lookup_list, &lookup_count);
        while (lookup_index < lookup_count) {
            uint16_t lookup_offset;
            hz_stream_read16(lookup_list, &lookup_offset);
            hz_array_push_back(lookup_offsets, lookup_offset);
            ++lookup_index;
        }
    }


    hz_stream_t *feature_list = hz_stream_create(data + feature_list_offset, 0, 0);


    {
        /* Parsing the FeatureList and applying selected Features */
        uint16_t feature_count;
        uint16_t feature_index = 0;
        hz_stream_read16(feature_list, &feature_count);
        HZ_LOG("feature_count: %u\n", feature_count);

        hz_map_t *feature_map = hz_map_create();

        /* fill map from feature type to offset */
        while (feature_index < feature_count) {
            hz_tag tag;
            uint16_t offset;
            hz_stream_read32(feature_list, &tag);
            hz_stream_read16(feature_list, &offset);
            hz_feature_t feature = hz_ot_feature_from_tag(tag);
            hz_map_set_value(feature_map, feature, offset);
            ++feature_index;
        }

        uint16_t wanted_feature_count = hz_array_size(wanted_features);
        uint16_t wanted_feature_index = 0;
        while (wanted_feature_index < wanted_feature_count) {
            hz_feature_t wanted_feature = hz_array_at(wanted_features, wanted_feature_index);

            if ( hz_map_value_exists(feature_map, wanted_feature) ) {
                /* feature is wanted and exists */
                hz_offset16 feature_offset = hz_map_get_value(feature_map, wanted_feature);
                hz_array_t *lookup_indices = hz_array_create();
                hz_ot_layout_feature_get_lookups(feature_list->data + feature_offset, lookup_indices);

                int i = 0;
                while (i < hz_array_size(lookup_indices)) {
                    uint16_t lookup_offset = hz_array_at(lookup_offsets, hz_array_at(lookup_indices, i));
                    hz_stream_t *lookup_table = hz_stream_create(lookup_list->data + lookup_offset, 0, 0);
                    hz_ot_layout_apply_gpos_lookup(face, lookup_table, wanted_feature, sect);
                    ++i;
                }

                hz_array_destroy(lookup_indices);
            }

            ++wanted_feature_index;
        }


        hz_map_destroy(feature_map);
    }

    return HZ_TRUE;
}


void
hz_ot_layout_lookups_substitute_closure(hz_face_t *face,
                                          const hz_set_t *lookups,
                                          hz_set_t *glyphs)
{

}

hz_bool
hz_ot_layout_lookup_would_substitute(hz_face_t *face,
                                     unsigned int lookup_index,
                                     const hz_id *glyphs,
                                     unsigned int glyph_count,
                                     hz_bool zero_context)
{

}


hz_bool
hz_ot_layout_parse_coverage(const uint8_t *data,
                            hz_map_t *map,
                            hz_array_t *id_arr)
{
    uint16_t coverage_format = 0;
    hz_stream_t *table = hz_stream_create(data,0,0);

    hz_stream_read16(table, &coverage_format);

    switch (coverage_format) {
        case 1: {
            uint16_t coverage_idx = 0;
            uint16_t coverage_glyph_count;
            hz_stream_read16(table, &coverage_glyph_count);
            while (coverage_idx < coverage_glyph_count) {
                uint16_t glyph_index;
                hz_stream_read16(table, &glyph_index);
//                hz_array_push_back(coverage_glyphs, glyph_index);
                if (id_arr != NULL)
                    hz_map_set_value(map, glyph_index, hz_array_at(id_arr, coverage_idx));
                else
                    hz_map_set_value(map, glyph_index, coverage_idx);

                ++coverage_idx;
            }

            return HZ_TRUE;
        }

        case 2: {
            uint16_t range_index = 0, range_count;
            hz_stream_read16(table, &range_count);

            /* Assuming ranges are ordered from 0 to glyph_count in order */
            while (range_index < range_count) {
                hz_id from, to;
                hz_range_rec_t range;
                uint16_t range_offset;
                uint32_t range_end;

                hz_stream_read16(table, &range.start_glyph_id);
                hz_stream_read16(table, &range.end_glyph_id);
                hz_stream_read16(table, &range.start_coverage_index);

                range_offset = 0;
                range_end = (range.end_glyph_id - range.start_glyph_id);
                while (range_offset <= range_end) {
                    from = range.start_glyph_id + range_offset;

                    if (id_arr != NULL)
                        to = hz_array_at(id_arr, range.start_coverage_index + range_offset);
                    else
                        to = range.start_coverage_index + range_offset;

                    hz_map_set_value(map, from, to);

//                    HZ_LOG("%d -> %d\n", from, to);
                    ++range_offset;
                }

                ++range_index;
            }

            return HZ_TRUE;
        }

        default: return HZ_FALSE;
    }
}

hz_section_node_t *
hz_next_node_not_of_class(hz_section_node_t *node,
                          hz_glyph_class_t ignored_classes,
                          uint16_t *skipped_nodes)
{
    assert(skipped_nodes != NULL);

    if (ignored_classes == HZ_GLYPH_CLASS_ZERO) {
        /* if no ignored classes, just give the next glyph directly as an optimization */
        (* skipped_nodes) ++;
        return node->next;
    }

    while (1) {
        (* skipped_nodes) ++;
        node = node->next;

        if (node == NULL) {
            /* break if next node is NULL, cannot keep searching */
            break;
        }

        if (~node->glyph.glyph_class & ignored_classes) {
            /* if not any of the class flags set, break, as we found what we want */
            break;
        }
    }

    return node;
}

hz_glyph_class_t
hz_ignored_classes_from_lookup_flags(hz_lookup_flag_t flags)
{
    hz_glyph_class_t ignored_classes = HZ_GLYPH_CLASS_ZERO;

    if (flags & HZ_LOOKUP_FLAG_IGNORE_MARKS) ignored_classes |= HZ_GLYPH_CLASS_MARK;
    if (flags & HZ_LOOKUP_FLAG_IGNORE_BASE_GLYPHS) ignored_classes |= HZ_GLYPH_CLASS_BASE;
    if (flags & HZ_LOOKUP_FLAG_IGNORE_LIGATURES) ignored_classes |= HZ_GLYPH_CLASS_LIGATURE;

    return ignored_classes;
}

typedef struct hz_ligature_t {
    uint16_t ligature_glyph;
    uint16_t component_count;
    uint16_t *component_glyph_ids;
} hz_ligature_t;

hz_ligature_t
hz_ot_layout_parse_ligature(const hz_byte *data) {
    hz_ligature_t ligature;
    hz_stream_t *table = hz_stream_create(data, 0, 0);
    hz_stream_read16(table, &ligature.ligature_glyph);
    hz_stream_read16(table, &ligature.component_count);
    ligature.component_glyph_ids = malloc(sizeof(uint16_t) * (ligature.component_count - 1));
    hz_stream_read16_n(table, ligature.component_count - 1, ligature.component_glyph_ids);
    return ligature;
}

/* if possible, apply ligature fit for sequence of glyphs
 * returns true if replacement occurred
 * */
hz_bool
hz_ot_layout_apply_fit_ligature(hz_ligature_t *ligatures,
                                uint16_t ligature_count,
                                hz_glyph_class_t ignored_classes,
                                hz_section_node_t *start_node)
{
    uint16_t ligature_index = 0;

    while (ligature_index < ligature_count) {
        uint16_t skipped_nodes = 0;
        uint16_t skip_index = 0;
        hz_section_node_t *step_node = start_node;
        hz_ligature_t *ligature = ligatures + ligature_index;
        hz_bool pattern_matches = HZ_TRUE;

        /* go over sequence and compare with current ligature */
        while (skip_index < ligature->component_count - 1) {
            step_node = hz_next_node_not_of_class(step_node, ignored_classes, &skipped_nodes);

            if (step_node == NULL) {
                pattern_matches = HZ_FALSE;
                break;
            }

            hz_id g1 = step_node->glyph.id;
            hz_id g2 = ligature->component_glyph_ids[skip_index];
            if (g1 != g2) {
                pattern_matches = HZ_FALSE;
                break;
            }

            ++ skip_index;
        }

        if (pattern_matches) {
            /* pattern matches, replace it */
            hz_section_rem_n_next_nodes(start_node, skipped_nodes);
            start_node->glyph.id = ligature->ligature_glyph;
            break;
        }

        ++ ligature_index;
    }
}

void
hz_ot_layout_apply_gsub_lookup(hz_face_t *face,
                               hz_stream_t *table,
                               hz_feature_t feature,
                               hz_section_t *sect)
{
    HZ_LOG("FEATURE '%c%c%c%c'\n", HZ_UNTAG(hz_ot_tag_from_feature(feature)));
    uint16_t lookup_type;
    uint16_t lookup_flags;
    uint16_t subtable_count;
    hz_stream_read16(table, &lookup_type);
    hz_stream_read16(table, &lookup_flags);
    hz_stream_read16(table, &subtable_count);

    HZ_LOG("lookup_type: %d\n", lookup_type);
    HZ_LOG("lookup_flag: %d\n", lookup_flags);
    HZ_LOG("subtable_count: %d\n", subtable_count);

    uint16_t subtable_index = 0;
    while (subtable_index < subtable_count) {
        hz_offset16 offset;
        hz_stream_read16(table, &offset);
        hz_stream_t *subtable = hz_stream_create(table->data + offset, 0, 0);
        uint16_t format;
        hz_stream_read16(subtable, &format);

        switch (lookup_type) {
            case HZ_GSUB_LOOKUP_TYPE_SINGLE_SUBSTITUTION: {
                if (format == 1) {
                    hz_offset16 coverage_offset;
                    int16_t id_delta;
                    hz_stream_read16(subtable, &coverage_offset);
                    hz_stream_read16(subtable, (uint16_t *) &id_delta);
                    /* NOTE: Implement */
                }
                else if (format == 2) {
                    hz_map_t *map_subst = hz_map_create();
                    hz_offset16 coverage_offset;
                    uint16_t glyph_count;
                    hz_array_t *subst = hz_array_create();
                    hz_stream_read16(subtable, &coverage_offset);
                    hz_stream_read16(subtable, &glyph_count);

                    /* Get destination glyph indices */
                    uint16_t dst_gidx;
                    for (dst_gidx = 0; dst_gidx < glyph_count; ++dst_gidx) {
                        hz_id substitute_glyph;
                        hz_stream_read16(subtable, &substitute_glyph);
                        hz_array_push_back(subst, substitute_glyph);
                    }

                    /* Read coverage offset */
                    hz_ot_layout_parse_coverage(subtable->data + coverage_offset, map_subst, subst);

                    /* Substitute glyphs */
                    hz_section_node_t *curr_node = sect->root;

                    while (curr_node != NULL) {
                        hz_id curr_id = curr_node->glyph.id;

                        if (hz_map_value_exists(map_subst, curr_id)) {
                            switch (feature) {
                                case HZ_FEATURE_ISOL:
                                case HZ_FEATURE_MEDI:
                                case HZ_FEATURE_MED2:
                                case HZ_FEATURE_INIT:
                                case HZ_FEATURE_FINA:
                                case HZ_FEATURE_FIN2:
                                case HZ_FEATURE_FIN3:
                                    if (hz_ot_shape_complex_arabic_join(feature, curr_node)) {
                                        curr_node->glyph.id = hz_map_get_value(map_subst, curr_id);
                                    }
                                    break;
                            }
                        }
                        curr_node = curr_node->next;
                    }

                    hz_array_destroy(subst);
                    hz_map_destroy(map_subst);
                }
                break;
            }

            case HZ_GSUB_LOOKUP_TYPE_MULTIPLE_SUBSTITUTION: {

                break;
            }

            case HZ_GSUB_LOOKUP_TYPE_ALTERNATE_SUBSTITUTION: {
                break;
            }

            case HZ_GSUB_LOOKUP_TYPE_LIGATURE_SUBSTITUTION: {
                if (format == 1) {
                    hz_offset16 coverage_offset;
                    uint16_t ligature_set_count;
                    hz_offset16 *ligature_set_offsets;
                    hz_map_t *coverage_map = hz_map_create();

                    hz_stream_read16(subtable, &coverage_offset);
                    hz_stream_read16(subtable, &ligature_set_count);
                    ligature_set_offsets = malloc(ligature_set_count * sizeof(uint16_t));
                    hz_stream_read16_n(subtable, ligature_set_count, ligature_set_offsets);
                    hz_ot_layout_parse_coverage(subtable->data + coverage_offset, coverage_map, NULL);

                    /* loop over every glyph in the section */
                    hz_section_node_t *node = sect->root;
                    while (node != NULL) {
                        hz_glyph_class_t ignored_classes = hz_ignored_classes_from_lookup_flags(lookup_flags);

                        /* glyph class part of ignored classes, skip */
                        if (node->glyph.glyph_class & ignored_classes)
                            goto skip_node;

                        if (hz_map_value_exists(coverage_map, node->glyph.id)) {
                            /* current glyph is covered, check pattern and replace */
                            hz_offset16 ligature_set_offset = ligature_set_offsets[ hz_map_get_value(coverage_map, node->glyph.id) ];
                            uint16_t ligature_count;
                            hz_stream_t *ligature_set = hz_stream_create(subtable->data + ligature_set_offset, 0, 0);
                            hz_stream_read16(ligature_set, &ligature_count);
                            hz_ligature_t *ligatures = malloc(sizeof(hz_ligature_t) * ligature_count);
                            uint16_t ligature_index = 0;

                            while (ligature_index < ligature_count) {
                                hz_offset16 ligature_offset;
                                hz_stream_read16(ligature_set, &ligature_offset);
                                ligatures[ligature_index] = hz_ot_layout_parse_ligature(ligature_set->data + ligature_offset);
                                ++ligature_index;
                            }

                            hz_ot_layout_apply_fit_ligature(ligatures, ligature_count, ignored_classes, node);
                        }

                        skip_node:
                        node = node->next;
                    }

                    free(ligature_set_offsets);
                    hz_map_destroy(coverage_map);
                } else {
                    /* error */
                }

                break;
            }

            case HZ_GSUB_LOOKUP_TYPE_CONTEXTUAL_SUBSTITUTION: {
                break;
            }

            case HZ_GSUB_LOOKUP_TYPE_CHAINED_CONTEXTS_SUBSTITUTION: {
                break;
            }

            case HZ_GSUB_LOOKUP_TYPE_EXTENSION_SUBSTITUTION: {
                break;
            }

            case HZ_GSUB_LOOKUP_TYPE_REVERSE_CHAINING_CONTEXTUAL_SINGLE_SUBSTITUTION: {
                break;
            }

            default:
                HZ_LOG("Invalid GSUB lookup type!\n");
                break;
        }

        ++subtable_index;
    }
}

typedef struct hz_entry_exit_record_t {
    hz_offset16 entry_anchor_offset, exit_anchor_offset;
} hz_entry_exit_record_t;

typedef struct hz_anchor_t {
    int16_t x_coord, y_coord;
} hz_anchor_t;

typedef struct hz_anchor_pair_t {
    hz_bool has_entry, has_exit;
    hz_anchor_t entry, exit;
} hz_anchor_pair_t;


hz_anchor_t
hz_ot_layout_read_anchor(const uint8_t *data) {
    hz_stream_t *stream = hz_stream_create(data, 0,0);
    hz_anchor_t anchor;

    uint16_t format;
    hz_stream_read16(stream, &format);

    HZ_ASSERT(format >= 1 && format <= 3);
    hz_stream_read16(stream, (uint16_t *) &anchor.x_coord);
    hz_stream_read16(stream, (uint16_t *) &anchor.y_coord);

    return anchor;
}

hz_anchor_pair_t
hz_ot_layout_read_anchor_pair(const uint8_t *subtable, const hz_entry_exit_record_t *rec) {
    hz_anchor_pair_t anchor_pair;

    anchor_pair.has_entry = rec->entry_anchor_offset ? HZ_TRUE : HZ_FALSE;
    anchor_pair.has_exit = rec->exit_anchor_offset ? HZ_TRUE : HZ_FALSE;

    if (anchor_pair.has_entry)
        anchor_pair.entry = hz_ot_layout_read_anchor(subtable + rec->entry_anchor_offset);

    if (anchor_pair.has_exit)
        anchor_pair.exit = hz_ot_layout_read_anchor(subtable + rec->exit_anchor_offset);

    return anchor_pair;
}

typedef struct hz_mark_record_t {
    uint16_t mark_class;
    hz_offset16 mark_anchor_offset;
} hz_mark_record_t;

hz_section_node_t *
hz_ot_layout_find_prev_with_class(hz_section_node_t *node, hz_glyph_class_t clazz)
{
    node = node->prev;
    while (node != NULL) {
        if (node->glyph.glyph_class == clazz) {
            /* found node with required class */
            break;
        }

        node = node->prev;
    }

    return node;
}

hz_section_node_t *
hz_ot_layout_find_next_with_class(hz_section_node_t *node, hz_glyph_class_t clazz)
{
    node = node->next;
    while (node != NULL) {
        if (node->glyph.glyph_class == clazz) {
            /* found node with required class */
            break;
        }

        node = node->next;
    }

    return node;
}

void
hz_ot_layout_apply_gpos_lookup(hz_face_t *face,
                               hz_stream_t *table,
                               hz_feature_t feature,
                               hz_section_t *sect)
{
    hz_lookup_table_t lookup;
    hz_stream_read16(table, &lookup.lookup_type);
    hz_stream_read16(table, &lookup.lookup_flags);
    hz_stream_read16(table, &lookup.subtable_count);

    HZ_LOG("lookup_type: %d\n", lookup.lookup_type);
    HZ_LOG("lookup_flag: %d\n", lookup.lookup_flags);
    HZ_LOG("subtable_count: %d\n", lookup.subtable_count);

    uint16_t subtable_index = 0;
    while (subtable_index < lookup.subtable_count) {
        hz_offset16 offset;
        hz_stream_read16(table, &offset);
        hz_stream_t *subtable = hz_stream_create(table->data + offset, 0, 0);
        uint16_t format;
        hz_stream_read16(subtable, &format);

        switch (lookup.lookup_type) {
            case HZ_GPOS_LOOKUP_TYPE_SINGLE_ADJUSTMENT: {
                break;
            }
            case HZ_GPOS_LOOKUP_TYPE_PAIR_ADJUSTMENT: {
                break;
            }
            case HZ_GPOS_LOOKUP_TYPE_CURSIVE_ATTACHMENT: {
                if (format == 1) {
                    /* 4k stack buffer */
                    uint8_t monobuf[4096];
                    cmas_mono_ma_t ma = cmas_mono_ma_create(monobuf, 4096);

                    hz_offset16 coverage_offset;
                    uint16_t record_count, record_index = 0;
                    hz_entry_exit_record_t *records;
                    hz_map_t *coverage_map = hz_map_create();

                    hz_stream_read16(subtable, &coverage_offset);
                    hz_stream_read16(subtable, &record_count);

                    records = cmas_mono_ma_alloc(&ma, sizeof(hz_entry_exit_record_t) * record_count);

                    while (record_index < record_count) {
                        hz_entry_exit_record_t *rec = &records[record_index];
                        hz_stream_read16(subtable, &rec->entry_anchor_offset);
                        hz_stream_read16(subtable, &rec->exit_anchor_offset);
                        ++record_index;
                    }

                    /* get coverage glyph to index map */
                    hz_ot_layout_parse_coverage(subtable->data + coverage_offset, coverage_map, NULL);


                    /* position glyphs */
                    hz_section_node_t *curr_node = sect->root;

                    while (curr_node != NULL) {
                        hz_glyph_t *g = &curr_node->glyph;

                        if (hz_map_value_exists(coverage_map, g->id)) {
                            uint16_t curr_idx = hz_map_get_value(coverage_map, g->id);
                            const hz_entry_exit_record_t *curr_rec = records + curr_idx;
                            hz_anchor_pair_t curr_pair = hz_ot_layout_read_anchor_pair(subtable->data, curr_rec);

                            if (curr_pair.has_exit && curr_node->next != NULL) {
                                uint16_t next_idx = hz_map_get_value(coverage_map, curr_node->next->glyph.id);
                                const hz_entry_exit_record_t *next_rec = records + next_idx;
                                hz_anchor_pair_t next_pair = hz_ot_layout_read_anchor_pair(subtable->data, next_rec);

                                int16_t y_delta = next_pair.entry.y_coord - curr_pair.exit.y_coord;
                                int16_t x_delta = next_pair.entry.x_coord - curr_pair.exit.x_coord;

//                                curr_node->glyph.x_offset = x_delta;
//                                curr_node->glyph.y_offset = y_delta;
                            }
                        }

                        curr_node = curr_node->next;
                    }

                    /* release resources */
                    cmas_mono_ma_free( &ma, records );
                    cmas_mono_ma_release( &ma );
                } else {
                    /* error */
                }

                break;
            }
            case HZ_GPOS_LOOKUP_TYPE_MARK_TO_BASE_ATTACHMENT: {
                /* attach mark to base glyph point */
                if (format == 1) {
                    uint8_t monobuf[4096];
                    cmas_mono_ma_t ma = cmas_mono_ma_create(monobuf, 4096);

                    hz_offset16 mark_coverage_offset;
                    hz_offset16 base_coverage_offset;
                    uint16_t mark_class_count;
                    hz_offset16 mark_array_offset;
                    hz_offset16 base_array_offset;
                    hz_map_t *mark_map = hz_map_create();
                    hz_map_t *base_map = hz_map_create();
                    hz_section_node_t *node;
                    hz_mark_record_t *mark_records;
                    uint16_t *base_anchor_offsets;

                    hz_stream_read16(subtable, &mark_coverage_offset);
                    hz_stream_read16(subtable, &base_coverage_offset);
                    hz_stream_read16(subtable, &mark_class_count);
                    hz_stream_read16(subtable, &mark_array_offset);
                    hz_stream_read16(subtable, &base_array_offset);

                    /* parse coverages */
                    hz_ot_layout_parse_coverage(subtable->data + mark_coverage_offset, mark_map, NULL);
                    hz_ot_layout_parse_coverage(subtable->data + base_coverage_offset, base_map, NULL);

                    /* parse arrays */
                    uint16_t mark_count;
                    uint16_t base_count;

                    {
                        /* parsing mark array */
                        hz_stream_t *marks = hz_stream_create(subtable->data + mark_array_offset, 0, 0);
                        hz_stream_read16(marks, &mark_count);
                        mark_records = cmas_mono_ma_alloc(&ma, sizeof(hz_mark_record_t) * mark_count);
                        uint16_t mark_index = 0;

                        while (mark_index < mark_count) {
                            hz_mark_record_t *mark = &mark_records[mark_index];

                            hz_stream_read16(marks, &mark->mark_class);
                            hz_stream_read16(marks, &mark->mark_anchor_offset);

                            ++mark_index;
                        }
                    }

                    {
                        /* parsing base array */
                        hz_stream_t *bases = hz_stream_create(subtable->data + base_array_offset, 0, 0);
                        hz_stream_read16(bases, &base_count);
                        base_anchor_offsets = malloc(base_count * mark_class_count * sizeof(uint32_t));
                        hz_stream_read16_n(bases, base_count * mark_class_count, base_anchor_offsets);
                    }


                    /* go over every glyph and position marks in relation to their base */
                    node = sect->root;
                    while (node != NULL) {
                        if (node->glyph.glyph_class & HZ_GLYPH_CLASS_MARK) {
                            /* position mark in relation to previous base if it exists */
                            hz_section_node_t *prev_base = hz_ot_layout_find_prev_with_class(node, HZ_GLYPH_CLASS_BASE);
                            hz_section_node_t *next_base = hz_ot_layout_find_next_with_class(node, HZ_GLYPH_CLASS_BASE);

                            if (prev_base != NULL) {
                                /* there actually is a previous base in the section */
                                if (hz_map_value_exists(base_map, prev_base->glyph.id) &&
                                hz_map_value_exists(mark_map, node->glyph.id)) {
                                    /* both the mark and base are covered by the table
                                     * position mark in relation to base glyph
                                     * */
                                    uint16_t mark_index = hz_map_get_value(mark_map, node->glyph.id);
                                    HZ_ASSERT(mark_index < mark_count);

                                    hz_mark_record_t *mark = &mark_records[ mark_index ];
                                    uint16_t base_index = hz_map_get_value(base_map, prev_base->glyph.id);

                                    HZ_ASSERT(mark->mark_class < mark_class_count);
                                    uint16_t base_anchor_offset = base_anchor_offsets[ base_index * mark_class_count + mark->mark_class ];

                                    /* check if the base anchor is NULL */
                                    if (base_anchor_offset != 0) {
                                        hz_anchor_t base_anchor = hz_ot_layout_read_anchor(subtable->data + base_array_offset + base_anchor_offset);
                                        hz_anchor_t mark_anchor = hz_ot_layout_read_anchor(subtable->data + mark_array_offset + mark->mark_anchor_offset);

                                        hz_metrics_t *base_metric = &face->metrics[prev_base->glyph.id];
                                        hz_metrics_t *mark_metric = &face->metrics[node->glyph.id];

                                        int32_t bw = base_metric->width;
                                        int32_t mw = mark_metric->width;
                                        int32_t bh = base_metric->height;
                                        int32_t mh = mark_metric->height;
                                        int32_t bby = base_metric->y_bearing;
                                        int32_t mby = mark_metric->y_bearing;
                                        int32_t bbx = base_metric->x_bearing;
                                        int32_t mbx = mark_metric->x_bearing;

                                        /* y base anchor */
                                        int32_t bay = base_anchor.y_coord - (bby - bh) - base_metric->y_min;
                                        int32_t may = (mark_anchor.y_coord - mark_metric->y_min) - (bby - bh) - (mby - mh);

                                        node->glyph.x_offset = 0;//(bbx - bw) - (mbx - mw);
                                        node->glyph.y_offset = 0;//(bby - bh) - (mby - mh);

//                                        node->glyph.x_offset = bbx - mbx;
//                                        node->glyph.y_offset = (mby - mh - (base_anchor.y_coord - bby))
//                                                - (bby - bh - (mark_anchor.y_coord - mby));
                                    }
                                }
                            }
                        }

                        node = node->next;
                    }

                    free(base_anchor_offsets);
                    cmas_mono_ma_free(&ma, mark_records);
                    cmas_mono_ma_release(&ma);
                    hz_map_destroy(mark_map);
                    hz_map_destroy(base_map);
                } else {
                    /* error */
                }

                break;
            }
            case HZ_GPOS_LOOKUP_TYPE_MARK_TO_LIGATURE_ATTACHMENT: {
                break;
            }
            case HZ_GPOS_LOOKUP_TYPE_MARK_TO_MARK_ATTACHMENT: {
                break;
            }
            case HZ_GPOS_LOOKUP_TYPE_CONTEXT_POSITIONING: {
                break;
            }
            case HZ_GPOS_LOOKUP_TYPE_CHAINED_CONTEXT_POSITIONING: {
                break;
            }
            case HZ_GPOS_LOOKUP_TYPE_EXTENSION_POSITIONING: {
                break;
            }
            default: {
                break;
            }
        }

        ++subtable_index;
    }
}

hz_tag
hz_ot_script_to_tag(hz_script_t script)
{
    switch (script) {
        case HZ_SCRIPT_ARABIC: return HZ_TAG('a','r','a','b');
        case HZ_SCRIPT_LATIN: return HZ_TAG('l','a','t','n');
        case HZ_SCRIPT_CJK: return HZ_TAG('h','a','n','i');
    }

    return 0;
}

hz_tag
hz_ot_language_to_tag(hz_language_t language)
{
    switch (language) {
        case HZ_LANGUAGE_ARABIC: return HZ_TAG('A','R','A',' ');
        case HZ_LANGUAGE_ENGLISH: return HZ_TAG('E','N','G',' ');
        case HZ_LANGUAGE_FRENCH: return HZ_TAG('F','R','A',' ');
        case HZ_LANGUAGE_JAPANESE: return HZ_TAG('J','A','N',' ');
        case HZ_LANGUAGE_URDU: return HZ_TAG('U','R','D',' ');
    }

    return 0;
}