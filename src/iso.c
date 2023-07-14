#define _XOPEN_SOURCE 700
#define _FILE_OFFSET_BITS 64

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <iconv.h>

#include "iso.h"

static
uint16_t LE_int16(uint8_t *iso_num) {
    return ((uint16_t) (iso_num[0] & 0xff) 
         | ((uint16_t) (iso_num[1] & 0xff) << 8));
}

static
uint32_t LE_int32(uint8_t *iso_num) {
    return ((uint32_t) (LE_int16(iso_num))
         | ((uint32_t) (LE_int16(iso_num + 2))) << 16);
}

static
uint64_t LE_int64(uint8_t *iso_num) {
    return ((uint64_t) (LE_int32(iso_num))
         | ((uint64_t) (LE_int32(iso_num + 4))) << 32);
}

static
void free_path_list(imn_cpath_t *path_header) {
    imn_cpath_t *tmp_segment;
    
    while (path_header != NULL) {
        tmp_segment = path_header->link;
        free(tmp_segment);
        path_header = tmp_segment;
    }
}

static
void free_extents(imn_extent_t *cur_extent) {
    imn_extent_t *tmp_extent;

    while (cur_extent != NULL) {
        tmp_extent = cur_extent->link;
        free(tmp_extent);
        cur_extent = tmp_extent;
    }
}

static
void free_rec_wrapper(imn_rawrec_wrapper_t *rec_wrapper) {
    
    if(rec_wrapper != NULL) {

        if (rec_wrapper->record_id != NULL) {
            free(rec_wrapper->record_id);
        }
        
        if(rec_wrapper->raw_rec != NULL) {
            free(rec_wrapper->raw_rec);
        }
    }
}

void imn_free_record(imn_record_t *rec) {
    
    free_extents(rec->extent_list);

    if (rec != NULL && rec->record_id != NULL) {
        free(rec->record_id);
    }
}

static
imn_error_t handle_iconv(char *from_code, char *to_code,
                            char *from_buff, size_t from_space,
                            char *to_buff, size_t to_space) {

    imn_error_t ret_val;
    iconv_t id_transform;
    size_t iconv_ret, real_space;

    id_transform = iconv_open(to_code, from_code);
    if (iconv_ret == -1) {
        ret_val = IMN_ENCODE_ERR;
        goto exit_normal;
    }

    iconv_ret = iconv(id_transform, &from_buff, &from_space,
                        &to_buff, &to_space);
    if (iconv_ret < 0) {
        ret_val = IMN_ENCODE_ERR;
        goto exit_normal;
    }

    if (iconv_close(id_transform) == -1) {
        ret_val = IMN_ENCODE_ERR;
        goto exit_normal;
    }

    ret_val = IMN_OK;
    exit_normal:
        return ret_val;
}

static
imn_error_t get_record_id(imn_iso_t *iso, imn_rawrec_wrapper_t *rec_wrapper) {

    imn_error_t ret_val;
    char *raw_id, *record_id;
    off_t id_offset;

    size_t read_ret, raw_len, id_length;
    int seek_ret, tmp_ret;

    if (iso == NULL || rec_wrapper == NULL) {
        ret_val = IMN_CODE_ERR;
        goto exit_early;
    }

    rec_wrapper->id_length = 0;
    rec_wrapper->record_id = NULL;

    id_offset = rec_wrapper->rec_offset + sizeof(imn_raw_record_t);
    raw_len = rec_wrapper->raw_rec->len_fi[0];

    seek_ret = fseeko(iso->iso_file, id_offset, SEEK_SET); 
    if (seek_ret != 0) {
        ret_val = IMN_ACCESS_ERR;
        goto exit_early;
    }

    raw_id = malloc(raw_len + 2);
    if (raw_id == NULL) {
        ret_val = IMN_ALLOC_ERR;
        goto exit_early;
    }
    memset(raw_id + raw_len, 0, 2);

    read_ret = fread(raw_id, 1, raw_len, iso->iso_file);
    if (read_ret != raw_len) {
        ret_val = IMN_ACCESS_ERR;
        goto exit_normal;
    }

    id_length = (raw_len * 3) / 2;
    record_id = malloc(id_length + 1);
    if (record_id == NULL) {
        ret_val = IMN_ALLOC_ERR;
        goto exit_normal;
    }

    if (raw_len == 1) {
        id_length = raw_len;
        memcpy(record_id, raw_id, raw_len + 1);

    } else {
        
        if (raw_len % 2 == 1) {
            ret_val = IMN_STD_ERR;
            goto exit_id;
        }

        char *tmp = record_id;
        ret_val = handle_iconv("UCS-2BE", "UTF-8",
                                raw_id, raw_len + 2,
                                record_id, id_length + 1);
        if (ret_val < 0) {
            goto exit_id;
        }

        id_length = strlen(record_id);
    }

    rec_wrapper->id_length = id_length;
    rec_wrapper->record_id = record_id;

    ret_val = IMN_OK;
    goto exit_normal;

    exit_id:
        free(record_id);
    exit_normal:
        free(raw_id);
    exit_early:
        return ret_val;
}

static
imn_error_t search_raw_record(imn_rawrec_wrapper_t *rec_wrapper,
        imn_iso_t *iso, imn_range_t *range, bool retrieve_id) {

    imn_error_t ret_val;
    imn_raw_record_t *raw_rec;

    off_t rec_start, rec_end;
    uint32_t lba_start, lba_end;
    uint16_t block_size;

    size_t read_ret;
    int seek_ret;

    if (rec_wrapper == NULL || iso == NULL || range == NULL) {
        ret_val = IMN_CODE_ERR;
        goto exit_normal;
    }

    rec_wrapper->raw_rec = NULL;
    rec_wrapper->rec_offset = 0;

    raw_rec = malloc(sizeof(*raw_rec));
    if (raw_rec == NULL) {
        ret_val = IMN_ALLOC_ERR;
    }

    rec_start = range->start;
    block_size = iso->desc->block_size;

    while (rec_start < range->end) {

        lba_start = rec_start / block_size;

        if (range->end - rec_start < sizeof(imn_raw_record_t)) {
            break;
        }

        seek_ret = fseeko(iso->iso_file, rec_start, SEEK_SET);
        if (seek_ret != 0) {
            ret_val = IMN_ACCESS_ERR;
            goto exit_normal;
        }

        read_ret = fread(raw_rec, sizeof(*raw_rec), 1, iso->iso_file);
        if (read_ret != 1) {
            ret_val = IMN_ACCESS_ERR;
            goto exit_normal;
        }
        
        if (raw_rec->len_dr[0] == 0) {
            rec_start = (lba_start + 1) * block_size;
            continue;
        }

        rec_end = rec_start + raw_rec->len_dr[0];
        lba_end = (rec_end) / block_size;

        // Non-aligned record or non-zero padding; violates ISO standard
        if (lba_start != lba_end) {
            ret_val = IMN_STD_ERR;
            goto exit_normal;
        }

        // Valid record; does not fit in provided range
        rec_end = rec_start + raw_rec->len_dr[0];
        if (rec_end > range->end) {
            break;
        }

        rec_wrapper->raw_rec = raw_rec;
        rec_wrapper->rec_offset = rec_start;

        if (retrieve_id) {
            ret_val = get_record_id(iso, rec_wrapper);
            if (ret_val != IMN_OK) {
                goto exit_normal;
            }
        }
        break;
    }

    ret_val = IMN_OK;
    exit_normal:
        return ret_val;
    
}

static
imn_error_t handle_lead_extent(imn_record_t *record,
        imn_rawrec_wrapper_t *rec_wrapper, imn_record_t *parent) {

    imn_error_t ret_val;
    imn_extent_t *lead_extent;
    imn_raw_record_t *raw_rec;

    size_t id_length;
    char *record_id;

    if (record == NULL || rec_wrapper == NULL) {
        ret_val = IMN_CODE_ERR;
        goto exit_normal;
    }
    raw_rec = rec_wrapper->raw_rec;

    record->is_hidden = (raw_rec->flags[0] & 0x1);
    record->is_dir = (raw_rec->flags[0] & 0x2);

    lead_extent = malloc(sizeof(*lead_extent));
    if (lead_extent == NULL) {
        ret_val = IMN_ALLOC_ERR;
        goto exit_normal;
    }

    lead_extent->lba_offset = LE_int32(&raw_rec->block[0]);
    lead_extent->data_length = LE_int32(&raw_rec->length[0]);
    lead_extent->link = NULL;

    record->extent_num = 1;
    record->extent_list = lead_extent;

    record->extent_span.start = rec_wrapper->rec_offset;
    record->extent_span.end = rec_wrapper->rec_offset +
                                rec_wrapper->raw_rec->len_dr[0];

    record->total_size = lead_extent->data_length;
    record->parent_dir = parent;

    id_length = rec_wrapper->id_length;
    if (!record->is_dir) {
        id_length -= 2;
    }

    record_id = malloc(id_length + 1);
    if (record_id == NULL) {
        ret_val = IMN_ALLOC_ERR;
        goto exit_normal;
    }
    memcpy(record_id, rec_wrapper->record_id, id_length);
    record_id[id_length] = '\0';

    record->id_length = id_length;
    record->record_id = record_id;

    ret_val = IMN_OK;
    exit_normal:
        return ret_val;
}

static
imn_error_t search_record(imn_record_t *record, imn_iso_t *iso,
        imn_record_t *parent, imn_range_t *global_range) {

    imn_error_t ret_val;

    imn_rawrec_wrapper_t rec_wrapper;
    imn_raw_record_t *raw_rec;
    imn_extent_t *cur_extent;
    imn_range_t local_range;

    bool multi_extent;
    off_t cur_loc;

    if (record == NULL || iso == NULL || global_range == NULL) {
        ret_val = IMN_CODE_ERR;
        goto exit_normal;
    }

    record->extent_list = NULL;
    record->extent_num = 0;

    local_range.start = global_range->start;
    local_range.end = global_range->end;

    ret_val = search_raw_record(&rec_wrapper, iso, &local_range, true);
    if (ret_val != IMN_OK) {
        goto exit_normal;
    }

    raw_rec = rec_wrapper.raw_rec;
    if (raw_rec == NULL) {
        ret_val = IMN_OK;
        goto exit_normal;
    }

    ret_val = handle_lead_extent(record, &rec_wrapper, parent);
    if (ret_val != IMN_OK) {
        goto exit_wrapper;
    }

    cur_loc = rec_wrapper.rec_offset + raw_rec->len_dr[0];
    multi_extent = (raw_rec->flags[0] & 0x80);
    cur_extent = record->extent_list;
    free_rec_wrapper(&rec_wrapper);

    while (multi_extent) {

        local_range.start = cur_loc;
        ret_val = search_raw_record(&rec_wrapper, iso, &local_range, false);
        if (ret_val != IMN_OK) {
            goto exit_extents;
        }
        raw_rec = rec_wrapper.raw_rec;

        // ISO-9660 violation: Addditional extent does not exist
        if (raw_rec == NULL) {
            ret_val = IMN_STD_ERR;
            goto exit_extents;
        }

        cur_extent->link = malloc(sizeof(*cur_extent));
        if (cur_extent->link == NULL) {
            ret_val = IMN_ALLOC_ERR;
            goto exit_extents;
        }
        cur_extent = cur_extent->link;

        cur_extent->lba_offset = LE_int32(&raw_rec->block[0]);
        cur_extent->data_length = LE_int32(&raw_rec->length[0]);
        cur_extent->link = NULL;
        
        record->extent_num += 1;
        record->total_size += cur_extent->data_length;

        cur_loc = rec_wrapper.rec_offset + raw_rec->len_dr[0];
        record->extent_span.end = cur_loc;

        multi_extent = (raw_rec->flags[0] & 0x80);
        free_rec_wrapper(&rec_wrapper);
    }

    ret_val = IMN_OK;
    goto exit_normal;

    exit_extents:
        free_extents(cur_extent);
    exit_wrapper:
        free_rec_wrapper(&rec_wrapper);
    exit_normal:
        return ret_val;
}

static
imn_error_t retrieve_desc(imn_vol_desc_t *desc, FILE *iso_file, off_t loc) {

    imn_error_t ret_val;

    imn_rawrec_wrapper_t rec_wrapper;
    imn_raw_vol_t raw_descriptor;
    imn_record_t *root_dir;
    imn_range_t range;

    size_t read_ret;
    int seek_ret;

    if (desc == NULL || iso_file == NULL || loc < 0) {
        ret_val = IMN_CODE_ERR;
        goto exit_normal;
    }

    seek_ret = fseeko(iso_file, loc, SEEK_SET);
    if (seek_ret != 0) {
        ret_val = IMN_ACCESS_ERR;
        goto exit_normal;
    }

    read_ret = fread(&raw_descriptor, sizeof(raw_descriptor), 1, iso_file);
    if (read_ret != 1) {
        ret_val = IMN_ACCESS_ERR;
        goto exit_normal;
    }

    desc->block_size = LE_int16(&raw_descriptor.block_size[0]);
    desc->path_table_size = LE_int32(&raw_descriptor.path_table_size[0]);
    desc->path_table_lba = LE_int32(&raw_descriptor.l_path_table_pos[0]);
    desc->lba_size = LE_int32(&raw_descriptor.vol_space_size[0]);

    root_dir = malloc(sizeof(*root_dir));
    if (root_dir == NULL) {
        ret_val = IMN_ALLOC_ERR;
        goto exit_normal;
    }

    rec_wrapper.raw_rec = (imn_raw_record_t *) &raw_descriptor.root_dir_record;
    rec_wrapper.rec_offset = loc + offsetof(imn_raw_vol_t, root_dir_record);
    rec_wrapper.id_length = 0;
    rec_wrapper.record_id = "\0";

    ret_val = handle_lead_extent(root_dir, &rec_wrapper, NULL);
    if (ret_val != IMN_OK) {
        goto exit_root;
    }
    desc->root_dir = root_dir;

    ret_val = IMN_OK;
    goto exit_normal;

    exit_root:
        free(root_dir);
    exit_normal:
        return ret_val;

}

imn_error_t imn_init(imn_iso_t *iso, char *iso_path, bool is_header) {

    imn_error_t ret_val;
    imn_vol_desc_t *desc;
    FILE *iso_file;

    if (iso == NULL || iso_path == NULL) {
        ret_val = IMN_ARGS_ERR;
        goto exit_normal;
    }

    iso_file = fopen(iso_path, "r");
    if (iso_file == NULL) {
        ret_val = IMN_PATH_ERR;
        goto exit_normal;
    }

    desc = malloc(sizeof(*desc));
    if (desc == NULL) {
        ret_val = IMN_ALLOC_ERR;
        goto exit_file;
    }

    ret_val = retrieve_desc(desc, iso_file, JOLIET_OFFSET);
    if (ret_val != IMN_OK) {
        goto exit_desc;
    }

    iso->iso_file = iso_file;
    iso->desc = desc;

    ret_val = IMN_OK;
    goto exit_normal;

    exit_file:
        fclose(iso_file);
    exit_desc:
        free(desc);
    exit_normal:
        return ret_val;
}

imn_error_t imn_traverse_dir(imn_iso_t *iso, imn_record_t *dir_record,
        imn_callback_t *callback, bool recursive) {
    
    imn_error_t ret_val;

    imn_extent_t *cur_extent;
    imn_record_t cur_record;
    imn_range_t range;

    uint16_t block_size;

    int call_ret;

    if (iso == NULL || dir_record == NULL || callback == NULL) {
        ret_val = IMN_ARGS_ERR;
        goto exit_normal;
    }

    if (!dir_record->is_dir) {
        ret_val = IMN_DIR_ERR;
        goto exit_normal;
    }

    block_size = iso->desc->block_size;
    cur_extent = dir_record->extent_list;

    // Handle multi-extent dirs
    while (cur_extent != NULL) {

        range.start = cur_extent->lba_offset * block_size;
        range.end = range.start + cur_extent->data_length;

        while (range.start < range.end) {

            ret_val = search_record(&cur_record, iso, dir_record, &range);
            if (ret_val != IMN_OK) {
                goto exit_normal;
            }

            if (cur_record.extent_num == 0) break;
            range.start = cur_record.extent_span.end;

            if (!cur_record.is_dir) {
                call_ret = callback->fn(&cur_record, callback->args);
                if (call_ret < 0) {
                    ret_val = IMN_CALLBACK_ERR;
                    goto exit_normal;
                }

            } else if (recursive &&
                        cur_record.record_id[0] != '\0' &&
                        cur_record.record_id[0] != '\1') {

                ret_val = imn_traverse_dir(iso, &cur_record,
                                            callback, recursive);

                if (ret_val != IMN_OK) {
                    goto exit_normal;
                }
            }
        }
        cur_extent = cur_extent->link;        
    }

    exit_normal:
        return ret_val;
}

imn_error_t imn_get_extents(imn_record_t *dir_record,
        imn_user_extent_t *list, int list_size) {

    imn_error_t ret_val;
    imn_extent_t *cur_extent;
    int list_index;
    off_t rel_offset;

    if (list == NULL || dir_record == NULL) {
        ret_val = IMN_ARGS_ERR;
        goto exit_normal;
    }

    if (list_size < dir_record->extent_num) {
        ret_val = IMN_ARGS_ERR;
        goto exit_normal;
    }

    list_index = 0;
    cur_extent = dir_record->extent_list;
    while (cur_extent != NULL) {
        list[list_index].lba_offset = cur_extent->lba_offset;
        list[list_index].data_length = cur_extent->data_length;
        list[list_index].file_name = dir_record->record_id;
        list[list_index].rel_offset = rel_offset;

        rel_offset += list[list_index].data_length;
        cur_extent = cur_extent->link;
        list_index++;
    }

    exit_normal:
        return ret_val;
}

imn_error_t imn_get_path(imn_record_t *record, char *buffer, int buffer_size) {

    imn_error_t ret_val;
    int str_pos, path_len;
    imn_record_t *cur_dir;

    imn_cpath_t *path_header, *tmp_segment;

    if (buffer == NULL || record == NULL) {
        ret_val = IMN_ARGS_ERR;
        goto exit_normal;
    }

    if (record->parent_dir == NULL) {
        ret_val = IMN_CODE_ERR;
        goto exit_normal;
    }

    cur_dir = record;
    path_header = NULL;

    // Build directory linked list
    path_len = -1;
    while (cur_dir != NULL) {

        if (cur_dir->id_length == 0) {
            cur_dir = cur_dir->parent_dir;
            continue;
        }

        path_len += cur_dir->id_length + 1;
        tmp_segment = path_header;

        path_header = malloc(sizeof(*path_header));
        if (path_header == NULL) {
            ret_val = IMN_ALLOC_ERR;
            goto exit_linked;
        }

        path_header->id_length = cur_dir->id_length;
        path_header->record_id = cur_dir->record_id;

        path_header->link = tmp_segment;
        cur_dir = cur_dir->parent_dir;
    }

    if (path_len >= buffer_size) {
        ret_val = IMN_MEM_ERR;
        goto exit_linked;
    }

    // Build full directory path in buffer
    str_pos = 0;
    while (path_header != NULL) {

        memcpy(buffer + str_pos, path_header->record_id,
                    path_header->id_length);

        str_pos += path_header->id_length + 1;
        buffer[str_pos-1] = '/';

        tmp_segment = path_header;
        path_header = path_header->link;
        free(tmp_segment);
    }
    buffer[str_pos-1] = '\0';

    ret_val = IMN_OK;
    goto exit_normal;

    exit_linked:
        free_path_list(path_header);
    exit_normal:
        return ret_val;
}
