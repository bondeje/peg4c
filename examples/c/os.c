#include "os.h"

#ifdef POSIX
#define INCLUDE_DEFINITIONS
#include "os_posix.h"
#endif

#ifdef _WIN32
#define INCLUDE_DEFINITIONS
#include "os_win32.h"
#endif

int print_file(char const * path, void * data) {
    if (is_file(path)) {
        printf("%s\n", path);
    }
    return 0;
}

void list_dir(char const * path, _Bool use_full_path) {
    iter_files(path, print_file, NULL, use_full_path);
}

struct file_in_dir {
    char const * target_file;
    _Bool present;
};

int file_in_dir_is_present(char const * file, void * data_) {
    struct file_in_dir * data = (struct file_in_dir *)data_;
    if (!strcmp(file, data->target_file)) {
        data->present = 1;
        return 1;
    }
    return 0;
}

_Bool file_in_dir(char const * path, char const * file) {
    struct file_in_dir data = {
        .target_file = file
    };
    iter_files(path, file_in_dir_is_present, &data, 0);
    return data.present;
}
