import cv2 as cv
import numpy as np


def triangle_points(center, size):
    half_size = size / 2
    height = (3**0.5) * half_size
    top = (center[0], center[1] - height / 2)
    bottom_left = (center[0] - half_size, center[1] + height / 2)
    bottom_right = (center[0] + half_size, center[1] + height / 2)
    return np.array([top, bottom_left, bottom_right], np.int32)


# Create a white image
width = 580
height = 768
base = np.ones((height, width, 3), np.uint8) * 255

# Calculate triangle points
size = 280
center = (width // 2, height // 2 - 120)
pts = triangle_points(center, size)

# Circle parameters
radius = size // 2 - 10
thickness = int(radius * 0.6)
radius = radius - thickness // 2

# Calculate rotated triangle points (for blue circle)
rotate_center = (int(pts[2][0]), int(pts[2][1]))
rot = cv.getRotationMatrix2D(rotate_center, -60, 1.0)
rotated_pts = cv.transform(np.array([pts]), rot)[0]

# Draw red and green circles and white triangle
cv.circle(base, (pts[0][0], pts[0][1]), radius, (66, 37, 255), thickness)
cv.circle(base, (pts[1][0], pts[1][1]), radius, (103, 219, 139), thickness)
cv.drawContours(base, [pts.astype(np.int32)], 0, [255, 255, 255], -1)

# Prepare blue circle with white triangle on separate image
blue = np.ones((height, width, 3), np.uint8) * 255
cv.circle(blue, (pts[2][0], pts[2][1]), radius, (255, 142, 12), thickness)
cv.drawContours(blue, [rotated_pts.astype(np.int32)], 0, [255, 255, 255], -1)

# Create mask and copy blue circle onto base image
diff = cv.absdiff(blue, 255)
mask = cv.cvtColor(diff, cv.COLOR_BGR2GRAY)
_, mask = cv.threshold(mask, 0, 255, cv.THRESH_BINARY)
cv.copyTo(blue, mask, base)

# Place text
font = cv.FONT_HERSHEY_COMPLEX
cv.putText(
    base,
    "OpenCV",
    (width - int(width * 0.99), height - int(height * 0.1)),
    font,
    4.5,
    (0, 0, 0),
    5,
    cv.LINE_AA,
)

cv.imshow("Logo", base)
cv.waitKey(0)
cv.destroyAllWindows()
