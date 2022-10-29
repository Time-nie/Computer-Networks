from flask import Flask, render_template

# 创建Flask类实例app
app = Flask(__name__)

# 通过装饰器设置函数URLs访问路径
@app.route('/')


# 定义设置网站首页的处理函数
def hello_world():
    return render_template("web.html")

if __name__ == '__main__':
    app.run()
