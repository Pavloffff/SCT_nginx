read CUR_DIR <<< $(pwd)
PATH_TO_NGINX_CONF="$CUR_DIR/nginx"

BUILD_NAME=with_neuro_robin
BUILD_PATH="$CUR_DIR/build"

NEURO_ROBIN_PATH="$CUR_DIR/module"

echo $BUILD_NAME

./auto/configure --prefix=PAPATH_TO_NGINX_CONF --user=$USER --build=$BUILD_NAME --builddir=$BUILD_PATH \
                 --with-http_ssl_module --with-http_perl_module=dynamic --with-stream=dynamic --with-stream_realip_module \
                 --with-cpp_test_module --with-cc=gcc --with-cpp=gcc --with-cc-opt="-g" --with-ld-opt="" \
                 --with-cpu-opt=cpu --with-pcre --with-debug 
                
                # --add-module=$NEURO_ROBIN_PATH 
                # --with-zlib=путь --with-zlib-opt=параметры --with-openssl=путь --with-openssl-opt=параметры 
