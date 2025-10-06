#include <iostream>
#include <cstdio>

void buildInfo() {
    printf("Running '%s', version %s\n", APP_NAME, APP_VERSION);
    printf("Major version: %s\n", APP_VERSION_MAJOR);
    printf("Minor version: %s\n", APP_VERSION_MINOR);
    printf("Patch version: %s\n", APP_VERSION_PATCH);
    printf("Branch: %s\n", APP_BRANCH);
    printf("Commit Hash: %s\n", APP_COMMIT_HASH);
    printf("Build Date: %s\n", APP_BUILD_DATE);
}

int main() {
    buildInfo();
    return 0;
}
