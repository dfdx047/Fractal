package dev.dfd.fractal.emulation.input

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.view.Surface
import dev.dfd.fractal.nativeinterface.NativeInput
import kotlin.math.sqrt


class DeviceMotionHandler(context: Context) : SensorEventListener {
    private data class SensorValues(val x: Float, val y: Float, val z: Float)

    private val sensorManager =
        context.getSystemService(Context.SENSOR_SERVICE) as SensorManager
    private val accelerometer =
        sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER)
    private val gyroscope =
        sensorManager.getDefaultSensor(Sensor.TYPE_GYROSCOPE)
    private val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    @Volatile
    private var deviceRotation = Surface.ROTATION_0

    /** Called on resume and whenever the display turns (a 180° turn between the two landscapes resumes nothing). */
    fun setDeviceRotation(deviceRotation: Int) {
        this.deviceRotation = deviceRotation
    }

    private val hasMotionData = accelerometer != null && gyroscope != null
    private var gyroValues = SensorValues(0f, 0f, 0f)
    private var hasGyroData = false
    private var accelValues = SensorValues(0f, 0f, 0f)
    private var hasAccelData = false
    private var isListening = false
    private var isActive = false

    // gravity seen by the accelerometer, in screen axes and g, smoothed: the position the device is held in
    private val gravity = FloatArray(3)
    private var hasGravity = false

    /**
     * Calibration: a rotation (row-major 3x3) applied to both sensors, so the position the player holds the device in
     * reads as the GamePad pose that aims straight ahead. Games that aim with the GamePad's tilt (Splatoon) otherwise
     * aim at the floor or the sky depending on how the phone is held.
     */
    private val calibration = loadCalibration()

    fun setIsListening(isListening: Boolean) {
        this.isListening = isListening
        if (isListening && !isActive) {
            startListening()
        } else if (!isListening && isActive) {
            stopListening()
        }
    }

    fun pauseListening() {
        if (isActive) {
            stopListening()
        }
    }

    fun resumeListening() {
        if (isListening && !isActive) {
            startListening()
        }
    }

    /** Takes the position the device is held in now as the pose that aims straight ahead. False without accelerometer data. */
    fun calibrate(): Boolean {
        if (!hasGravity) return false
        val len = sqrt(gravity[0] * gravity[0] + gravity[1] * gravity[1] + gravity[2] * gravity[2])
        if (len < 0.5f) return false // falling or shaking, no clear "down"
        val g = floatArrayOf(gravity[0] / len, gravity[1] / len, gravity[2] / len)
        // smallest rotation taking the measured "up" g to AIM_AHEAD_UP: axis v = g x t, cos = g . t
        val t = AIM_AHEAD_UP
        val vx = g[1] * t[2] - g[2] * t[1]
        val vy = g[2] * t[0] - g[0] * t[2]
        val vz = g[0] * t[1] - g[1] * t[0]
        val c = g[0] * t[0] + g[1] * t[1] + g[2] * t[2]
        val r = FloatArray(9)
        if (c < -0.999f) {
            // the opposite pose: half turn around the screen's x axis (perpendicular to t, which lies in y-z)
            r[0] = 1f; r[4] = -1f; r[8] = -1f
        } else {
            // R = I + [v]x + [v]x^2 / (1 + c)
            val k = 1f / (1f + c)
            r[0] = 1f - (vy * vy + vz * vz) * k
            r[1] = -vz + vx * vy * k
            r[2] = vy + vx * vz * k
            r[3] = vz + vx * vy * k
            r[4] = 1f - (vx * vx + vz * vz) * k
            r[5] = -vx + vy * vz * k
            r[6] = -vy + vx * vz * k
            r[7] = vx + vy * vz * k
            r[8] = 1f - (vx * vx + vy * vy) * k
        }
        r.copyInto(calibration)
        saveCalibration()
        NativeInput.resetDeviceMotion()
        return true
    }

    fun resetCalibration() {
        IDENTITY.copyInto(calibration)
        saveCalibration()
        NativeInput.resetDeviceMotion()
    }

    private fun startListening() {
        if (!hasMotionData || isActive) {
            return
        }

        isActive = true
        hasGravity = false

        NativeInput.setDeviceMotionEnabled(true)
        sensorManager.registerListener(this, gyroscope, SensorManager.SENSOR_DELAY_GAME)
        sensorManager.registerListener(this, accelerometer, SensorManager.SENSOR_DELAY_GAME)
    }

    private fun stopListening() {
        if (!hasMotionData || !isActive) {
            return
        }

        isActive = false

        NativeInput.setDeviceMotionEnabled(false)
        sensorManager.unregisterListener(this)
    }

    override fun onSensorChanged(event: SensorEvent) {
        val values = event.values

        if (event.sensor.type == Sensor.TYPE_GYROSCOPE) {
            gyroValues = getSensorEventValues(values)
            hasGyroData = true
        } else if (event.sensor.type == Sensor.TYPE_ACCELEROMETER) {
            accelValues = getSensorEventValues(values)
            hasAccelData = true
            trackGravity(accelValues)
        }

        if (hasAccelData && hasGyroData) {
            hasAccelData = false
            hasGyroData = false
            val gyro = calibrated(gyroValues)
            val accel = calibrated(accelValues)
            NativeInput.onDeviceMotion(
                event.timestamp,
                gyro.x,
                gyro.y,
                gyro.z,
                accel.x / SensorManager.GRAVITY_EARTH,
                accel.y / SensorManager.GRAVITY_EARTH,
                accel.z / SensorManager.GRAVITY_EARTH,
            )
        }
    }

    override fun onAccuracyChanged(sensor: Sensor, accuracy: Int) {}

    private fun trackGravity(accel: SensorValues) {
        val x = accel.x / SensorManager.GRAVITY_EARTH
        val y = accel.y / SensorManager.GRAVITY_EARTH
        val z = accel.z / SensorManager.GRAVITY_EARTH
        if (!hasGravity) {
            gravity[0] = x; gravity[1] = y; gravity[2] = z
            hasGravity = true
            return
        }
        // about a quarter of a second at SENSOR_DELAY_GAME: steady enough to ignore a tap on the screen
        val a = 0.1f
        gravity[0] += (x - gravity[0]) * a
        gravity[1] += (y - gravity[1]) * a
        gravity[2] += (z - gravity[2]) * a
    }

    private fun calibrated(v: SensorValues): SensorValues {
        val r = calibration
        return SensorValues(
            r[0] * v.x + r[1] * v.y + r[2] * v.z,
            r[3] * v.x + r[4] * v.y + r[5] * v.z,
            r[6] * v.x + r[7] * v.y + r[8] * v.z,
        )
    }

    private fun loadCalibration(): FloatArray {
        val saved = prefs.getString(KEY_CALIBRATION, null)?.split(',')?.mapNotNull { it.toFloatOrNull() }
        return if (saved != null && saved.size == 9) saved.toFloatArray() else IDENTITY.copyOf()
    }

    private fun saveCalibration() {
        prefs.edit().putString(KEY_CALIBRATION, calibration.joinToString(",")).apply()
    }

    private fun getSensorEventValues(values: FloatArray): SensorValues {
        val x: Float
        val y: Float
        val z = values[2]

        when (deviceRotation) {
            Surface.ROTATION_0 -> {
                x = values[0]
                y = values[1]
            }

            Surface.ROTATION_90 -> {
                x = -values[1]
                y = values[0]
            }

            Surface.ROTATION_180 -> {
                x = -values[0]
                y = -values[1]
            }

            else /*Surface.ROTATION_270*/ -> {
                x = values[1]
                y = -values[0]
            }
        }
        return SensorValues(x, y, z)
    }

    companion object {
        private const val PREFS_NAME = "motion"
        private const val KEY_CALIBRATION = "gyro_calibration"
        private val IDENTITY = floatArrayOf(1f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 1f)

        // "up" in screen axes for the pose that aims straight ahead: the device upright with its top tilted 30° towards
        // the player. Measured in Splatoon on the RP5 (calibration written over root, device still): upright aims at the
        // floor, -15° a little down, -30° level, -45° at the sky
        private val AIM_AHEAD_UP = floatArrayOf(0f, 0.8660254f, -0.5f)
    }
}
