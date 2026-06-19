from flask import Flask, render_template, request

app = Flask(__name__)

@app.route("/")
def home():
    return render_template("index.html")

@app.route("/generate", methods=["POST"])
def generate():
    return {"status":"demo","message":"Connect your image-processing backend here."}

if __name__ == "__main__":
    app.run(debug=True)
