
FIND_PACKAGE(LibArchive REQUIRED)
FIND_PACKAGE(ZLIB REQUIRED)
FIND_LIBRARY(YAML_LIBRARY NAMES yaml)
FIND_LIBRARY(SOLV_LIBRARY NAMES solv)

INCLUDE_DIRECTORIES("/usr/include/solv")

ADD_EXECUTABLE(rubygems2solv rubygems2solv.c common_write.c rubygems_parser.c gem_version_bump.c)
TARGET_LINK_LIBRARIES(rubygems2solv ${LibArchive_LIBRARIES} ${ZLIB_LIBRARIES} ${YAML_LIBRARY} ${SOLV_LIBRARY})

ADD_EXECUTABLE(rubygems2susetags rubygems2susetags.c common_write.c rubygems_parser.c gem_version_bump.c)
TARGET_LINK_LIBRARIES(rubygems2susetags ${LibArchive_LIBRARIES} ${ZLIB_LIBRARIES} ${YAML_LIBRARY} ${SOLV_LIBRARY})

ADD_EXECUTABLE(gemdump gemdump.c rubygems_parser.c)
TARGET_LINK_LIBRARIES(gemdump ${LibArchive_LIBRARIES} ${ZLIB_LIBRARIES} ${YAML_LIBRARY} ${SOLV_LIBRARY})
