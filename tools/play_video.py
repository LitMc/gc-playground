import numpy as np
import cv2 as cv
import argparse

ap = argparse.ArgumentParser()
ap.add_argument("-v", "--video", required=True, help="path to input video file")

cap = cv.VideoCapture(ap.parse_args().video)

width = int(cap.get(cv.CAP_PROP_FRAME_WIDTH))
height = int(cap.get(cv.CAP_PROP_FRAME_HEIGHT))
print(f"Video resolution: {width} x {height}")

fps = cap.get(cv.CAP_PROP_FPS)
print(f"Frames per second: {fps}")

frame_count = int(cap.get(cv.CAP_PROP_FRAME_COUNT))
print(f"Total number of frames: {frame_count}")

total_seconds = frame_count / fps
hour = int(total_seconds / 3600)
min = int((total_seconds - hour * 3600) / 60)
sec = int(total_seconds - hour * 3600 - min * 60)
frac = total_seconds - int(total_seconds)
print(f"Total duration: {hour:02}:{min:02}:{sec:02}.{int(frac * 100):02}")

fourcc = int(cap.get(cv.CAP_PROP_FOURCC))
print(f"FOURCC code: {fourcc.to_bytes(4, byteorder='little')}")

while cap.isOpened():
    ret, frame = cap.read()

    # if frame is read correctly ret is True
    if not ret:
        print("Can't receive frame (stream end?). Exiting ...")
        break
    gray = cv.cvtColor(frame, cv.COLOR_BGR2GRAY)

    cv.imshow('frame', gray)
    if cv.waitKey(1) == ord('q'):
        break

cap.release()
cv.destroyAllWindows()
