from pathlib import Path

import cv2 as cv

out_path = Path("movies/output.mp4").resolve()
out_path.unlink(missing_ok=True)

cap = cv.VideoCapture(0)

# Define the codec and create VideoWriter object
fourcc = cv.VideoWriter_fourcc(*"H264")
width = int(cap.get(cv.CAP_PROP_FRAME_WIDTH))
height = int(cap.get(cv.CAP_PROP_FRAME_HEIGHT))
out = cv.VideoWriter(str(out_path), fourcc, fps=20.0, frameSize=(width, height))

print(f"Output: {out_path.absolute()}")
print(f"Frame size: {width}x{height}")
print("Press 'q' to stop recording ...")

while cap.isOpened():
    ret, frame = cap.read()
    if not ret:
        print("Can't receive frame (stream end?). Exiting ...")
        break

    # flip the frame vertically
    frame = cv.flip(frame, 0)

    # write the flipped frame
    out.write(frame)

    cv.imshow("frame", frame)
    if cv.waitKey(1) == ord("q"):
        break

# Release everything if job is finished
cap.release()
out.release()
cv.destroyAllWindows()
