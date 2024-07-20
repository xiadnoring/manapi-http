# Generate keys
openssl req -x509 -nodes -days 365 -newkey rsa:2048 -keyout cert.key -out cert.crt

# Generate Root Certificate
# You can import this file into your browser or system for testing purposes only
cat cert.key cert.crt > rootca.crt