conan create .  \
              -o "manapihttp/*:shared=True" -o "manapihttp/*:json_debug=True" \
              -o "manapihttp/*:wolfssl_dependency=False" -o "manapihttp/*:openssl_dependency=True" \
              -o "manapihttp/*:quiche_dependency=False" -o "manapihttp/*:tquic_dependency=False" \
              -o "manapihttp/*:curl_dependency=True" -o "manapihttp/*:gmp_dependency=False" \
              -o "manapihttp/*:zlib_dependency=False" -o "manapihttp/*:lib=True" -o "libcurl/*:shared=True" \
              -o "openssl/*:shared=True" -o "libev/*:shared=True" --build=missing