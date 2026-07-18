#include <stdio.h>
#include <string.h>
#include "disk.h"

int main(int argc, char *argv[])
{
    int rv = -1;
    char volume_name[32] = "UNTITLED";
    if (argc % 2 != 0)
    {
        fprintf(stderr, "ERROR: Invalid number of arguments!!\n");
        goto err1;
    }
    if (argc > 2)
    {
        if (!strcmp(argv[1], "-l"))
        {
            strncpy(volume_name, argv[2], 32);
        }
    }
    DiskInterface* disk = disk_open(argv[--argc]);
    cache *cache = NULL;
    if (disk_format(disk, cache, volume_name)) goto err2;
    disk_close(disk);
    return 0;
err2:
    disk_close(disk);
    return rv;
err1:
    fprintf(stderr, "ERROR: Could not format disk!!\n");
    return rv;
}
