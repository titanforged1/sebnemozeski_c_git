#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>
#include <openssl/sha.h>
#include <dirent.h>
#include <curl/curl.h> // libcurl kütüphanesi

typedef struct {
    char name[256];
    unsigned char sha1[20];
    char mode[16];
} TreeEntry;

int compare_entries(const void *a, const void *b) {
    return strcmp(((TreeEntry *) a)->name, ((TreeEntry *) b)->name);
}

// Git nesnelerini Zlib ile sıkıştırıp .git/objects altına kaydeden ortak fonksiyon
void write_object_to_disk(
    const unsigned char *merged_data,
    long total_size
    , unsigned char *out_sha1
) {
    SHA1(merged_data, total_size, out_sha1);

    char hex_hash[41];

    for (int i = 0; i < 20; i++) {
        sprintf(hex_hash + (i * 2),
                "%02x",
                out_sha1[i]);
    }

    hex_hash[40] = '\0';

    uLongf compressed_size = compressBound(total_size);
    unsigned char *compressed_data = malloc(compressed_size);

    compress(
        (Bytef *) compressed_data,
        &compressed_size,
        (const Bytef *) merged_data,
        total_size
    );

    char directory_path[256];

    char full_file_path[256];

    snprintf(directory_path,
             sizeof(directory_path),
             ".git/objects/%.2s",
             hex_hash);

    mkdir(directory_path, 0755);

    snprintf(full_file_path,
             sizeof(full_file_path),
             "%s/%s", directory_path,
             hex_hash + 2);

    FILE *file = fopen(full_file_path, "wb");

    if (file != NULL) {
        fwrite(compressed_data,
               1,
               compressed_size, file
        );

        fclose(file);
    } else {
        fprintf(stderr,
                "Failed to create object file: %s\n",
                full_file_path);
    }

    free(compressed_data);
}

// Dosyayı blob nesnesi olarak diske yazan fonksiyon
void hash_blob_file(const char *filepath, unsigned char *out_sha1) {
    FILE *file = fopen(filepath, "rb");

    if (file == NULL) {
        memset(out_sha1, 0, 20);

        return;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *content = malloc(file_size);
    fread(content, 1, file_size, file);
    fclose(file);

    char header[64];
    int header_size = snprintf(header, sizeof(header), "blob %ld", file_size);
    long total_size = header_size + 1 + file_size;

    unsigned char *merged_data = malloc(total_size);
    memcpy(merged_data, header, header_size);
    merged_data[header_size] = '\0';
    memcpy(merged_data + header_size + 1, content, file_size);

    write_object_to_disk(merged_data, total_size, out_sha1);

    free(content);
    free(merged_data);
}

// tree nesnesi oluşturan ana fonksiyon
void write_tree_recursive(const char *dir_path, unsigned char *out_tree_sha1) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        memset(out_tree_sha1, 0, 20);
        return;
    }

    struct dirent *entry;
    TreeEntry entries[2048];
    int entry_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, ".git") == 0) {
            continue;
        }

        char full_path[512];

        snprintf(full_path,
                 sizeof(full_path),
                 "%s/%s",
                 dir_path,
                 entry->d_name);

        struct stat st;

        if (stat(full_path, &st) == -1) continue;

        strcpy(entries[entry_count].name, entry->d_name);

        if (S_ISDIR(st.st_mode)) {
            strcpy(entries[entry_count].mode, "40000");
            write_tree_recursive(full_path, entries[entry_count].sha1);
        } else {
            if (st.st_mode & S_IXUSR) {
                strcpy(entries[entry_count].mode, "100755");
            } else {
                strcpy(entries[entry_count].mode, "100644");
            }

            hash_blob_file(full_path, entries[entry_count].sha1);
        }
        entry_count++;
    }
    closedir(dir);

    qsort(entries,
          entry_count,
          sizeof(TreeEntry),
          compare_entries);

    long body_capacity = 1024 * 1024;
    unsigned char *body_data = malloc(body_capacity);
    long body_size = 0;

    for (int i = 0; i < entry_count; i++) {
        int written = snprintf(
            (char *) (body_data + body_size),
            body_capacity - body_size,
            "%s %s",
            entries[i].mode,
            entries[i].name);

        body_size += written;

        body_data[body_size] = '\0';
        body_size += 1;

        memcpy(body_data + body_size, entries[i].sha1, 20);
        body_size += 20;
    }

    char header[64];
    int header_size = snprintf(header,
                               sizeof(header),
                               "tree %ld",
                               body_size);

    long total_size = header_size + 1 + body_size;

    unsigned char *merged_tree_data = malloc(total_size);
    memcpy(merged_tree_data, header, header_size);
    merged_tree_data[header_size] = '\0';
    memcpy(merged_tree_data + header_size + 1, body_data, body_size);

    write_object_to_disk(merged_tree_data, total_size, out_tree_sha1);

    free(body_data);
    free(merged_tree_data);
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    if (argc < 2) {
        fprintf(stderr,
                "Usage: ./your_program.sh <command> [<args>]\n");
        return 1;
    }
    const char *command = argv[1];

    if (strcmp(command, "init") == 0) {
        if (mkdir(".git", 0755) == -1 ||
            mkdir(".git/objects", 0755) == -1 ||
            mkdir(".git/refs", 0755) == -1) {
            fprintf(stderr, "Failed to create directories: %s\n", strerror(errno));
            return 1;
        }

        FILE *headFile = fopen(".git/HEAD", "w");
        if (headFile == NULL) {
            fprintf(stderr, "Failed to create .git/HEAD file: %s\n", strerror(errno));
            return 1;
        }
        fprintf(headFile, "ref: refs/heads/main\n");
        fclose(headFile);

        printf("Initialized git directory\n");
    } else if (strcmp(command, "cat-file") == 0) {
        char *blob_sha = argv[3];
        char blob_path_file[256];

        snprintf(blob_path_file,
                 sizeof(blob_path_file),
                 ".git/objects/%.2s/%s",
                 blob_sha,
                 blob_sha + 2);

        FILE *blobFile = fopen(blob_path_file, "rb");
        if (blobFile == NULL) {
            fprintf(stderr, "File not found: %s \n", blob_path_file);
            return 1;
        }

        fseek(blobFile, 0, SEEK_END);
        long blobFile_size = ftell(blobFile);
        fseek(blobFile, 0, SEEK_SET);

        unsigned char *compressed_blob_data = malloc(blobFile_size);
        fread(compressed_blob_data, 1, blobFile_size, blobFile);
        fclose(blobFile);


        unsigned long uncompressed_size = 1024 * 1024;
        unsigned char *uncompressed_blob_data = malloc(uncompressed_size);

        uncompress((Bytef *) uncompressed_blob_data,
                   (uLongf *) &uncompressed_size,
                   (const Bytef *) compressed_blob_data,
                   blobFile_size);

        char *blob_content = strchr((char *) uncompressed_blob_data, '\0');
        if (blob_content != NULL) {
            blob_content++;
            printf("%s", blob_content);
        }

        free(uncompressed_blob_data);
        free(compressed_blob_data);
    } else if (strcmp(command, "hash-object") == 0) {
        if (argc < 4 || strcmp(argv[2], "-w") != 0) {
            fprintf(stderr, "Usage: ./your_program.sh hash-object -w <file_name>\n");
            return 1;
        }

        char *hash_file_name = argv[3];
        unsigned char out_sha1[20];

        hash_blob_file(hash_file_name, out_sha1);

        for (int i = 0; i < 20; i++) {
            printf("%02x", out_sha1[i]);
        }
        printf("\n");
    } else if (strcmp(command, "ls-tree") == 0) {
        if (argc < 4 || strcmp(argv[2], "--name-only") != 0) {
            fprintf(stderr, "Usage: ./your_program.sh ls-tree --name-only\n");
            return 1;
        }

        char *tree_hash = argv[3];
        char tree_path_file[256];

        snprintf(tree_path_file,
                 sizeof(tree_path_file),
                 ".git/objects/%.2s/%s",
                 tree_hash,
                 tree_hash + 2);

        FILE *tree_file = fopen(tree_path_file, "rb");
        if (tree_file == NULL) {
            printf("Failed to create tree object\n");
            return 1;
        }

        fseek(tree_file, 0, SEEK_END);
        long tree_file_size = ftell(tree_file);
        fseek(tree_file, 0, SEEK_SET);

        unsigned char *compressed_data = malloc(tree_file_size);
        fread(compressed_data, 1, tree_file_size, tree_file);
        fclose(tree_file);

        unsigned long uncompressed_size = 1024 * 1024;
        unsigned char *uncompressed_data = malloc(uncompressed_size);

        uncompress((Bytef *) uncompressed_data,
                   (uLongf *) &uncompressed_size,
                   (const Bytef *) compressed_data,
                   tree_file_size);

        char *ptr = strchr((char *) uncompressed_data, '\0');
        if (ptr != NULL) {
            ptr++;
        }

        char *end_of_data = (char *) (uncompressed_data + uncompressed_size);

        while (ptr < end_of_data) {
            char *space_ptr = strchr(ptr, ' ');
            if (space_ptr == NULL) break;
            char *name_ptr = space_ptr + 1;
            printf("%s\n", name_ptr);
            char *null_ptr = strchr(name_ptr, '\0');
            if (null_ptr == NULL) break;
            ptr = null_ptr + 1 + 20;
        }

        free(uncompressed_data);
        free(compressed_data);
    } else if (strcmp(command, "write-tree") == 0 || strcmp(command, "--write-tree") == 0) {
        unsigned char final_tree_sha1[20];

        write_tree_recursive(".", final_tree_sha1);

        for (int i = 0; i < 20; i++) {
            printf("%02x", final_tree_sha1[i]);
        }
        printf("\n");
    } else if (strcmp(command, "commit-tree") == 0) {
        if (argc < 7 || strcmp(argv[3], "-p") != 0 || strcmp(argv[5], "-m") != 0) {
            fprintf(
                stderr,
                "Usage: ./your_program.sh commit-tree <tree_sha> -p <commit_sha> -m <message>"
            );
        }
        char *tree_sha = argv[2];
        char *parent_sha = argv[4];
        char *message = argv[6];

        char buffer[1024];
        int len = snprintf(buffer,
                           sizeof(buffer),
                           "tree %s\nparent %snauthor John Doe <john@example.com> 1234567890 +0000\n John Doe <john@example.com> 1234567890 +0000\n\n%s\n",
                           tree_sha,
                           parent_sha,
                           message);

        char header[64];
        int header_len = snprintf(header, sizeof(header), "commit %d", len);

        long total_size = header_len + 1 + len;
        unsigned char *commit_data = malloc(total_size);
        memcpy(commit_data,header,header_len);
        commit_data[header_len] = '\0';
        memcpy(commit_data + header_len + 1 ,buffer , len);

        unsigned char commit_sha1[20];
        write_object_to_disk(commit_data, total_size, commit_sha1);

        for (int i = 0; i < 20; i++ ) {
            printf("%02x", commit_sha1[i]);
        }
        printf("\n");
        free(commit_data);

    } else {
        fprintf(stderr, "Unknown command %s\n", command);
        return 1;
    }


    return
            0;
}
