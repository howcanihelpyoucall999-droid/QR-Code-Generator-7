from flask import Flask, render_template, request, send_file
from PIL import Image
from utils.a4_generator import create_a4_sheet
import os

app = Flask(__name__)
UPLOAD='uploads'
OUTPUT='output'

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/generate', methods=['POST'])
def generate():
    file=request.files['image']
    copies=int(request.form.get('copies',8))
    path=os.path.join(UPLOAD,file.filename)
    file.save(path)

    img=Image.open(path).convert('RGB')
    out=os.path.join(OUTPUT,'passport_photo.jpg')
    img.save(out, quality=95)

    a4=os.path.join(OUTPUT,'a4_sheet.jpg')
    create_a4_sheet(out,a4,copies)

    return {'photo':'/download/photo','a4':'/download/a4'}

@app.route('/download/photo')
def photo():
    return send_file('output/passport_photo.jpg')

@app.route('/download/a4')
def a4():
    return send_file('output/a4_sheet.jpg')

if __name__=='__main__':
    app.run(debug=True)
