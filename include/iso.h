#define _XOPEN_SOURCE 700
#define _FILE_OFFSET_BITS 64

#ifndef ISO_MINI_H
#define ISO_MINI_H

#include <stdint.h>
#include <sys/types.h>
#include <stdio.h>

#define JOLIET_OFFSET 0x8800
#define BP(a,b) [(b) - (a) + 1]

/**** Raw ISO-9660 Structs ****/

typedef struct {
    uint8_t vol_desc_type            BP(1, 1);
    uint8_t std_identifier           BP(2, 6);
    uint8_t vol_desc_version         BP(7, 7);
    uint8_t unused1                  BP(8, 8);
    uint8_t system_id                BP(9, 40);
    uint8_t volume_id                BP(41, 72);
    uint8_t unused2                  BP(73, 80);
    uint8_t vol_space_size           BP(81, 88);
    uint8_t unused3                  BP(89, 120);
    uint8_t vol_set_size             BP(121, 124);
    uint8_t vol_seq_number           BP(125, 128);
    uint8_t block_size               BP(129, 132);
    uint8_t path_table_size          BP(133, 140);
    uint8_t l_path_table_pos         BP(141, 144);
    uint8_t opt_l_path_table_pos     BP(145, 148);
    uint8_t m_path_table_pos         BP(149, 152);
    uint8_t opt_m_path_table_pos     BP(153, 156);
    uint8_t root_dir_record          BP(157, 190);
    uint8_t vol_set_id               BP(191, 318);
    uint8_t publisher_id             BP(319, 446);
    uint8_t data_prep_id             BP(447, 574);
    uint8_t application_id           BP(575, 702);
    uint8_t copyright_file_id        BP(703, 739);
    uint8_t abstract_file_id         BP(740, 776);
    uint8_t bibliographic_file_id    BP(777, 813);
    uint8_t vol_creation_time        BP(814, 830);
    uint8_t vol_modification_time    BP(831, 847);
    uint8_t vol_expiration_time      BP(848, 864);
    uint8_t vol_effective_time       BP(865, 881);
    uint8_t file_structure_version   BP(882, 882);
    uint8_t reserved1                BP(883, 883);
    uint8_t app_use                  BP(884, 1395);
    uint8_t reserved2                BP(1396, 2048);

} imn_raw_vol_t;

typedef struct {
    uint8_t len_dr                   BP(1, 1);
    uint8_t len_xa                   BP(2, 2);
    uint8_t block                    BP(3, 10);
    uint8_t length                   BP(11, 18);
    uint8_t recording_time           BP(19, 25);
    uint8_t flags                    BP(26, 26);
    uint8_t file_unit_size           BP(27, 27);
    uint8_t interleave_gap_size      BP(28, 28);
    uint8_t vol_seq_number           BP(29, 32);
    uint8_t len_fi                   BP(33, 33);

} imn_raw_record_t;

typedef struct {
    uint8_t len_di                   BP(1, 1);
    uint8_t len_xa                   BP(2, 2);
    uint8_t block                    BP(3, 6);
    uint8_t parent                   BP(7, 8);

} imn_raw_pt_record_t;


/**** Internal Structs ****/

typedef struct {
    off_t start;
    off_t end;

} imn_range_t;

typedef struct linked_path_s {

    uint32_t id_length;
    char *record_id;
    struct linked_path_s *link;

} imn_cpath_t;

typedef struct {

    size_t length;
    char *string;

} imn_string_t;

typedef struct {
    
    imn_raw_record_t *raw_rec;
    off_t rec_offset;

    char *record_id;
    size_t id_length;

} imn_rawrec_wrapper_t;


/**** API Structs ****/

typedef struct {

    uint32_t lba_offset;
    uint32_t data_length;

    off_t rel_offset;
    char *file_name;

} imn_user_extent_t;

typedef struct imn_extent_s {

    uint32_t lba_offset;
    uint32_t data_length;
    struct imn_extent_s *link;

} imn_extent_t;

typedef struct imn_record_t {

    struct imn_record_t *parent_dir;
    off_t total_size;

    bool is_hidden;
    bool is_dir;

    imn_range_t extent_span;
    uint32_t extent_num;
    imn_extent_t *extent_list;

    uint32_t id_length;
    char *record_id;

} imn_record_t;

typedef struct {
    uint32_t lba_size;
    uint16_t block_size;

    imn_record_t *root_dir;

    uint32_t path_table_size;
    uint32_t path_table_lba;

} imn_vol_desc_t;

typedef struct {
	imn_vol_desc_t *desc;

    bool is_header;
	FILE *iso_file;

} imn_iso_t;

typedef struct {

    int (*fn)(imn_record_t *, void *);
    void *args;

} imn_callback_t;


/**** API Errors ****/

typedef enum {

    IMN_OK,
    IMN_CODE_ERR,

    IMN_STD_ERR,
    IMN_ARGS_ERR,

    IMN_ALLOC_ERR,
    IMN_MEM_ERR,

    IMN_DIR_ERR,

    IMN_PATH_ERR,
    IMN_ACCESS_ERR,

    IMN_CALLBACK_ERR,
    IMN_ENCODE_ERR,
    

} imn_error_t;


/**** API Functions ****/

imn_error_t imn_init(imn_iso_t *iso, char *iso_path, bool is_header);

imn_error_t imn_traverse_dir(imn_iso_t *iso, imn_record_t *dir_record,
        imn_callback_t *callback, bool recursive);

imn_error_t imn_get_extents(imn_record_t *dir_record,
        imn_user_extent_t *list, int list_size);

imn_error_t imn_get_path(imn_record_t *record, char *buffer, int buffer_size);

void imn_free_record(imn_record_t *rec);

#endif
