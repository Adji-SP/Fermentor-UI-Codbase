// Host-only regression checks; not part of the firmware or its release image.
#[allow(dead_code)]
#[path = "../../../IO-Fermentor-Kopi/Fermentor/src/sensors.rs"]
mod sensors;

#[cfg(test)]
mod tests {
    use super::sensors::{ph_from_voltage, ph_uncorrected_from_voltage, PH_TEMPORARY_OFFSET};

    fn voltage_for_original_ph(ph: f32) -> f32 {
        (1500.0 + (7.0 - ph) * (2032.44 - 1500.0) / 3.0) / 1000.0
    }

    fn close(actual: Option<f32>, expected: f32) {
        assert!((actual.expect("expected a valid pH") - expected).abs() < 0.0001);
    }

    #[test]
    fn image_comparison_applies_once_and_preserves_original() {
        let voltage = voltage_for_original_ph(5.92);
        assert_eq!(PH_TEMPORARY_OFFSET, -1.10);
        close(ph_uncorrected_from_voltage(voltage), 5.92);
        for _ in 0..100 {
            close(ph_from_voltage(voltage), 4.82);
        }
    }

    #[test]
    fn offset_does_not_suppress_or_flatten_drift() {
        for (original, corrected) in [(6.1, 5.0), (5.58, 4.48), (5.9, 4.8)] {
            close(ph_from_voltage(voltage_for_original_ph(original)), corrected);
        }
    }

    #[test]
    fn original_default_calibration_is_unchanged() {
        close(ph_uncorrected_from_voltage(1.5), 7.0);
        close(ph_uncorrected_from_voltage(2.03244), 4.0);
        close(ph_from_voltage(1.5), 5.9);
        close(ph_from_voltage(2.03244), 2.9);
    }

    #[test]
    fn nonfinite_or_out_of_range_originals_remain_invalid() {
        for voltage in [f32::NAN, f32::INFINITY, f32::NEG_INFINITY, -1.0, 0.0, 4.096] {
            assert!(ph_uncorrected_from_voltage(voltage).is_none());
            assert!(ph_from_voltage(voltage).is_none());
        }
        // Even though 14.5 - 1.1 would be in range, do not rescue bad originals.
        assert!(ph_from_voltage(voltage_for_original_ph(14.5)).is_none());
    }

    #[test]
    fn corrected_out_of_range_is_error_not_clamped_to_zero() {
        let voltage = voltage_for_original_ph(0.5);
        close(ph_uncorrected_from_voltage(voltage), 0.5);
        assert!(ph_from_voltage(voltage).is_none());
    }
}
