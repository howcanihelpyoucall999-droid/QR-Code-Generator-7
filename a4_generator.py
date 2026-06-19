from PIL import Image

def create_a4_sheet(photo_path, output_path, copies=8):
    sheet=Image.new('RGB',(2480,3508),'white')
    photo=Image.open(photo_path).resize((413,531))
    x=y=50
    for i in range(copies):
        sheet.paste(photo,(x,y))
        x+=450
        if x>1900:
            x=50
            y+=600
    sheet.save(output_path, quality=95)
