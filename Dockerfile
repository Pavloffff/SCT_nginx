FROM redhat/ubi8-minimal:8.7 AS base

RUN microdnf install gcc gcc-c++
RUN microdnf install pcre-devel
RUN microdnf install openssl-devel
RUN microdnf install perl-devel
RUN microdnf install perl-ExtUtils-Embed
RUN microdnf install make

WORKDIR /app
COPY . .

RUN /app/auto/configure --with-http_ssl_module --with-mail --with-stream \
    --with-stream_realip_module --with-cpp_test_module --with-cc=gcc --with-cpp=gcc \
    --with-cc-opt="-fPIC" \
    --with-cpu-opt=cpu --with-pcre --with-debug --with-http_perl_module


# WORKDIR /app/objs
# RUN cp -r /app/src/ /app/objs/
RUN make -j4
RUN make install

ENTRYPOINT [ "/usr/local/nginx/sbin/nginx" ]