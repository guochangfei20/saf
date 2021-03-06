cmake_minimum_required(VERSION 2.8.8)
#set(CMAKE_VERBOSE_MAKEFILE on)

project (saf)
include_directories(./ src deps)
link_directories(lib)
# osx会设置动态库的install_name为@rpath/libxxx.dylib，否则它会设置成生成时的路径的绝对路径
set(CMAKE_MACOSX_RPATH 1)


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(THIRD_DEPS_PATH ${CMAKE_SOURCE_DIR}/deps)
# 设置可执行文件路径
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/dist)
# 设置生成库路径
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
# 对于多配置的工程，也统一指定，方便在生成联合库时明确位置进行解压
foreach( OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES} )
  string( TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG )
  set( CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/lib )
  set( CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${CMAKE_SOURCE_DIR}/lib )
endforeach( OUTPUTCONFIG CMAKE_CONFIGURATION_TYPES )
# 重新定义src目录
set(CMAKE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)


#设置版本号
SET( SAF_VERSION 1.0.0 )
#从"1.0.0"解析出_MAJOR, _MINOR, _PATCH版本
MACRO(saf_parse_version VERSION_TEXT OUPUT_PREFIX)
  SET(VERSION_REGEX "[0-9]+\\.[0-9]+\\.[0-9]+(-[a-zA-Z0-9_]+)?")
  IF( ${VERSION_TEXT} MATCHES ${VERSION_REGEX} )
    STRING(REGEX MATCHALL "[0-9]+|-([A-Za-z0-9_]+)" VERSION_PARTS ${VERSION_TEXT})
    LIST(GET VERSION_PARTS 0 ${OUPUT_PREFIX}_MAJOR)
    LIST(GET VERSION_PARTS 1 ${OUPUT_PREFIX}_MINOR)
    LIST(GET VERSION_PARTS 2 ${OUPUT_PREFIX}_PATCH)
  ENDIF()
ENDMACRO()
saf_parse_version( ${SAF_VERSION} SAF_VERSION )


#设置打印颜色
if(NOT WIN32)
  string(ASCII 27 Esc)
  set(ColourReset "${Esc}[m")
  set(ColourBold  "${Esc}[1m")
  set(Red         "${Esc}[31m")
  set(Green       "${Esc}[32m")
  set(Yellow      "${Esc}[33m")
  set(Blue        "${Esc}[34m")
  set(Magenta     "${Esc}[35m")
  set(Cyan        "${Esc}[36m")
  set(White       "${Esc}[37m")
  set(BoldRed     "${Esc}[1;31m")
  set(BoldGreen   "${Esc}[1;32m")
  set(BoldYellow  "${Esc}[1;33m")
  set(BoldBlue    "${Esc}[1;34m")
  set(BoldMagenta "${Esc}[1;35m")
  set(BoldCyan    "${Esc}[1;36m")
  set(BoldWhite   "${Esc}[1;37m")
endif()

#生成协议文件
if(EXISTS "${CMAKE_SOURCE_DIR}/saf_packet.pb.h")
else()
  execute_process(COMMAND
    protoc -I=${CMAKE_SOURCE_DIR}/ --cpp_out=${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/saf_packet.proto
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
  message(STATUS "${Green}Generating ${CMAKE_SOURCE_DIR}/saf_packet.pb.h${ColourReset}")
endif()

#生成版本文件
configure_file( "${CMAKE_SOURCE_DIR}/saf_version.h.in"
  "${CMAKE_SOURCE_DIR}/saf_version.h"
  NEWLINE_STYLE UNIX )

include (ExternalProject)
# 生成sails版本库，ExternalProject默认是使用cmake进行编译
ExternalProject_Add(sails_proj
  PREFIX sails
  SOURCE_DIR ${THIRD_DEPS_PATH}/sails
  #CMAKE_ARGS -DCMAKE_BUILD_TYPE:STRING=Release
  # 因为可能会编译到debug,release目录下
  INSTALL_COMMAND find . -name libsails.a | xargs -I {} cp {} ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/
  )





# 生成saf程序
set(SAF_SRC
  src/saf.cc
  src/server.cc
  src/handle_rpc.cc
  src/module_load.cc
  src/service_register.cc
  src/config.cc
  src/monitor.cc
  src/saf_packet.pb.cc)
add_executable(saf ${SAF_SRC})
# 连接程序时，它会自动把不是标准的路径加入到rpath中
target_link_libraries(saf protobuf dl sails pthread)
add_dependencies(saf sails_proj)




# 生成safclient库
set(SAF_CLIENT_SRC
  src/rpc_client.cc
  src/rpc_channel.cc
  src/rpc_controller.cc
  src/saf_packet.pb.cc
  )
add_library(safclient_static STATIC ${SAF_CLIENT_SRC})
add_library(safclient SHARED ${SAF_CLIENT_SRC})
target_link_libraries(safclient sails protobuf )
add_dependencies(safclient sails_proj)
# 为了防止OUTPUT_NAME设置新的target时清除已经存在的同名target
set_target_properties(safclient PROPERTIES CLEAN_DIRECT_OUTPUT 1)
set_target_properties(safclient_static PROPERTIES OUTPUT_NAME "safclient")
# 设置动态库的版本，api兼容版本
set_target_properties(safclient PROPERTIES VERSION ${SAF_VERSION} SOVERSION ${SAF_VERSION_MAJOR})

# 这里为了使用者方便，生成一个联合静态库
add_custom_target(safclient_static_combined ALL
  COMMAND find ./ -name "libsafclient_combined.a" | xargs rm -f
  COMMAND ${CMAKE_AR} x libsafclient.a
  COMMAND ${CMAKE_AR} x  libsails.a
  COMMAND ${CMAKE_AR} rc libsafclient_combined.a *.o
  COMMAND ${CMAKE_RANLIB} libsafclient_combined.a
  COMMAND rm *.o
  DEPENDS safclient_static sails_proj
  WORKING_DIRECTORY ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}
  )


#################################php###################################
option(PHPLIB "Set to ON to generate an php shared library" ${PHPLIB})

if (PHPLIB)
  # 生成php swig包裹类
  if(EXISTS "${CMAKE_SOURCE_DIR}/php_safclient.cc")
  else()
    execute_process(COMMAND
      swig -php -c++  -o ${CMAKE_SOURCE_DIR}/php_safclient.cc ${CMAKE_SOURCE_DIR}/saf_swig.i
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      )
    message(STATUS "${Green}Generating php_safclient.cc by saf_swig.i ${ColourReset}")
  endif()

  # 生成php sdk
  set(SAF_CLIENT_SRC_PHP
    ${SAF_CLIENT_SRC}
    src/php_safclient.cc
    )
  # 得到php include路径
  execute_process(COMMAND
    php-config --includes
    OUTPUT_VARIABLE php_include
    OUTPUT_STRIP_TRAILING_WHITESPACE #因为output末尾是回车，应该去掉
    )
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${php_include}")
  
  SET(php_create_flag " -Wl,-flat_namespace")
  FOREACH(symbol
      __efree
      __emalloc
      __estrdup
      __estrndup
      __object_init_ex
      __object_init
      __zend_get_parameters_array_ex
      __zend_list_find
      __zval_copy_ctor
      _add_property_zval_ex
      _alloc_globals
      _compiler_globals
      _convert_to_double
      _convert_to_long
      __convert_to_string
      _convert_to_boolean
      _gc_remove_zval_from_buffer
      _zend_error
      _zend_hash_find
      _zend_register_internal_class_ex
      _zend_register_list_destructors_ex
      _zend_register_resource
      _zend_rsrc_list_get_rsrc_type
      _zend_wrong_param_count
      _zval_used_for_init
      __zend_hash_add_or_update
      __zval_copy_ctor_func
      _zend_get_constant
      _zend_lookup_class
      _zend_register_long_constant
      _zval_is_true
      )
    SET(php_create_flag 
      "${php_create_flag},-U,${symbol}")
  ENDFOREACH(symbol)

  add_library(php_safclient SHARED ${SAF_CLIENT_SRC_PHP})
  set_target_properties(php_safclient PROPERTIES LINK_FLAGS ${php_create_flag})
  target_link_libraries(php_safclient sails protobuf)

endif()

#################################php###################################



#############################client for ios############################
# 因为cmake在处理externalproject+ios时会有问题，所以当是ios要生成client时使用它
# 这里最小化了生成client所需要的全部源码(没有含protobuf)，所以不像上面那样包含整个
# sails工程
if(XCODE)
  set(SAF_CLIENT_IOS_SRC
    deps/sails/net/connector.cc
    deps/sails/base/string.cc
    deps/sails/base/timer.cc
    deps/sails/base/event_loop.cc
    deps/sails/base/thread_pool.cc
    src/rpc_client.cc
    src/rpc_channel.cc
    src/rpc_controller.cc
    src/saf_packet.pb.cc
    )
  add_library(safclient_ios ${SAF_CLIENT_IOS_SRC})
endif()
#############################client for ios############################
