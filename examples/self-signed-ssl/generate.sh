# Generate keys

openssl req -x509 -newkey rsa:2048 -keyout cert.key -out cert.crt -days 999
openssl rsa -in cert.key -out cert.key.new

# Generate Root Certificate
mv cert.key.new cert.key
# You can import this file into your browser or system for testing purposes only
cat cert.key cert.crt > rootca.crt
