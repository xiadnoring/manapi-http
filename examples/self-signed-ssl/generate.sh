## Generate keys
#
#openssl req -x509 -newkey rsa:2048 -keyout cert.key -out cert.crt -days 999 -subj "/C=RU/ST=Bashkort Republic/L=Ufa/O=Manapi Inc/OU=Manapi Http/CN=localhost"
#openssl rsa -in cert.key -out cert.key.new
#
## Generate Root Certificate
#mv cert.key.new cert.key
## You can import this file into your browser or system for testing purposes only
#cat cert.key cert.crt > rootca.crt

# Generate keys
openssl req -x509 -nodes -days 999 -newkey rsa:2048 -keyout cert.key -out cert.crt
rm rootca.crt
# Generate Root Certificate
# You can import this file into your browser or system for testing purposes only
cat cert.key cert.crt > rootca.crt