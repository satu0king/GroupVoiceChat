from flask import Flask, render_template,request
import os, signal
import subprocess
app = Flask(__name__, static_url_path='', static_folder='')
app.config['SEND_FILE_MAX_AGE_DEFAULT'] = 0

@app.route('/hello')
def hello_world():
    return 'Hello, World!'

@app.route('/')
def index():
    return app.send_static_file('./index.html')

@app.route('/call', methods=['POST'])
def upload():
    print(request.get_data(as_text=True))

    url = request.get_data(as_text=True).split(":")
    ip = url[0]
    port = url[1]

    
    subprocess.Popen(["bin/client", ip, port])
    
    return "Ok"