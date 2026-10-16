# Fixtures de dados de disparo (tst_exif::shootingInfo). Rodar num container:
#   docker run --rm -v "$PWD/tests/data/exif":/out python:3.12-slim sh -c \
#     "pip install -q piexif pillow && python /out/gen_shooting.py"
import piexif
from PIL import Image
out = "/out/"
def make(name, ifd0, exif):
    img = Image.new("RGB", (64, 48), (128, 128, 128))
    data = piexif.dump({"0th": ifd0, "Exif": exif, "GPS": {}, "1st": {}, "thumbnail": None})
    img.save(out + name, "JPEG", quality=90, exif=data)
make("shooting.jpg",
     {piexif.ImageIFD.Make: b"Canon", piexif.ImageIFD.Model: b"Canon EOS R5", piexif.ImageIFD.Orientation: 1},
     {piexif.ExifIFD.ExposureTime: (1, 200), piexif.ExifIFD.FNumber: (28, 10), piexif.ExifIFD.ISOSpeedRatings: 800,
      piexif.ExifIFD.FocalLength: (85, 1), piexif.ExifIFD.LensModel: b"RF85mm F1.2 L USM",
      piexif.ExifIFD.DateTimeOriginal: b"2026:10:15 18:30:00"})
make("shooting_long.jpg",
     {piexif.ImageIFD.Make: b"NIKON CORPORATION", piexif.ImageIFD.Model: b"NIKON D850"},
     {piexif.ExifIFD.ExposureTime: (2, 1), piexif.ExifIFD.FNumber: (8, 1), piexif.ExifIFD.ISOSpeedRatings: 64,
      piexif.ExifIFD.FocalLength: (245, 10)})
