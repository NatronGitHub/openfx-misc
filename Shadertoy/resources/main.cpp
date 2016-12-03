#include <cstdio>
#include <cstring>
#include <string>

using namespace std;
int main()
{
    char line[1024];
    FILE* fp = std::fopen("Shadertoy.txt", "r");
    //int i = 0;
    while (1) {
        if (std::fgets(line, sizeof(line), fp) == NULL) {
            break;
        }
        //++i;
        //printf("%3d: %s", i, line);
        if (line[0] == '#') { // skip comments
            continue;
        }
        // a line looks like
        //    {"Ball",                            "ball.frag.glsl",                 99,-1,-1,-1},
        const char* desc = std::strchr(line, '"');
        if (desc == NULL) {
            continue;
        }
        ++desc;
        const char* desc_end = std::strchr(desc, '"');
        if (desc_end == NULL) {
            continue;
        }
        string description(desc, desc_end);
        ++desc_end;
        const char* file = std::strchr(desc_end, '"');
        if (file == NULL) {
            continue;
        }
        ++file;
        const char* file_end = std::strchr(file, '"');
        if (file_end == NULL) {
            continue;
        }
        string filename(file, file_end);
        printf("%s,%s\n", description.c_str(), filename.c_str());
    }
}
