read CUR_DIR <<< $(pwd)
PATH_TO_NGINX_CONF="$CUR_DIR/build"

BUILD_NAME=with_neuro_robin
BUILD_PATH="$CUR_DIR"

NEURO_ROBIN_PATH="$CUR_DIR/module"

echo $BUILD_NAME

./auto/configure --prefix=$PATH_TO_NGINX_CONF --user=$USER --build=$BUILD_NAME --builddir=$BUILD_PATH \
                 --with-http_ssl_module --with-mail  --with-stream \
                 --with-stream_realip_module --with-cpp_test_module --with-cc=gcc --with-cpp=gcc --with-cc-opt="-g" \
                 --with-cpu-opt=cpu --with-pcre --with-debug 
                
                # --add-module=$NEURO_ROBIN_PATH  
                # --with-zlib=путь --with-zlib-opt=параметры --with-openssl=путь --with-openssl-opt=параметры --with-http_perl_module
