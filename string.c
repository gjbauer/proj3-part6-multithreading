#include "string.h"

int
count_l(const char *path)
{
    int c=0;
    if (strlen(path)==1) return c;
    for(int i=0; path[i]; i++) {
        if (path[i]=='/') c++;
    }
    return c;
}

char* parent_path(const char *path, int l)
{
    char *pp = malloc(PATH_MAX);
    memset(pp, '\0', PATH_MAX);

    if (l <= 1) {
        pp[0] = '/';
        return pp;
    }

    const char *start = path;
    const char *end;
    int segment = 0;

    // Find the last slash before the final component
    end = path + strlen(path) - 1;
    while (end > path && *end != '/') {
        end--;
    }

    if (end == path) {
        pp[0] = '/';
    } else {
        strncpy(pp, path, end - path);
        pp[end - path] = '\0';
    }

    return pp;
}

char* get_name(const char *path)
{
    const char *last_slash = strrchr(path, '/');
    if (!last_slash) {
        return strdup(path);
    }
    return strdup(last_slash + 1);
}

char *split(const char *path, int n)
{
    const char *start = path;
    const char *end;
    int segment = 0;
    
    // Handle NULL cases
    if (!path || n <= 0) {
        // We should never reach this
        return NULL;
    }
    
    // Find the nth segment
    while (*start && segment < n) {
        // Skip leading slashes
        while (*start == '/') start++;
        if (!*start) break;
        
        segment++;
        if (segment == n) {
            // Found our segment, find its end
            end = start;
            while (*end && *end != '/') end++;
            
            // Copy the segment
            size_t len = end - start;
            char *buf = (char*)calloc(len + 1, sizeof(char));
            strncpy(buf, start, len);
            buf[len] = '\0';
            return buf;
        }
        
        // Skip to next slash
        while (*start && *start != '/') start++;
    }
    
    // Segment not found
    return NULL;
}
