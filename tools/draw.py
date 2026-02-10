import cv2 as cv
import numpy as np

# Create a black image
img = np.zeros((512, 512, 3), np.uint8)

# Draw a diagonal blue line with thickness of 5 px
cv.line(img, (0, 0), (511, 511), (255, 0, 0), 5)

# Draw a rectangle
cv.rectangle(img, (384, 0), (510, 128), (0, 255, 0), 3)

# Draw a circle
cv.circle(img, (447, 63), 63, (0, 0, 255), -1)

# Draw an ellipse
cv.ellipse(img, (256, 256), (100, 50), 0, 0, 180, 255, -1)

# Draw a polygon
pts = np.array([[10, 5], [20, 30], [70, 20], [50, 10]], np.int32)
pts = pts.reshape((-1, 1, 2))
cv.polylines(img, [pts], True, (0, 255, 255))

# Add text
font = cv.FONT_HERSHEY_SIMPLEX
cv.putText(img, "OpenCV", (10, 500), font, 4, (255, 255, 255), 2, cv.LINE_AA)

cv.imshow("Image", img)
while True:
    if cv.waitKey(1) == ord("q"):
        break

cv.destroyAllWindows()
