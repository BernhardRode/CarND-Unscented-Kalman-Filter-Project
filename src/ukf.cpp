#include "ukf.h"
#include "Eigen/Dense"
#include <iostream>

using namespace std;
using Eigen::MatrixXd;
using Eigen::VectorXd;
using std::vector;

/**
 * Initializes Unscented Kalman filter
 */
UKF::UKF()
{
  // if this is false, laser measurements will be ignored (except during init)
  is_initialized_ = false;

  // if this is false, laser measurements will be ignored (except during init)
  use_laser_ = true;

  // if this is false, radar measurements will be ignored (except during init)
  use_radar_ = true;

  // predicted sigma points matrix
  Xsig_pred_ = MatrixXd(n_x_, 2 * n_aug_ + 1);

  // Process noise standard deviation longitudinal acceleration in m/s^2
  std_a_ = 1;

  // Process noise standard deviation yaw acceleration in rad/s^2
  std_yawdd_ = 1;

  // Laser measurement noise standard deviation position1 in m
  std_laspx_ = 0.15;

  // Laser measurement noise standard deviation position2 in m
  std_laspy_ = 0.15;

  // Radar measurement noise standard deviation radius in m
  std_radr_ = 0.3;

  // Radar measurement noise standard deviation angle in rad
  std_radphi_ = 0.03;

  // Radar measurement noise standard deviation radius change in m/s
  std_radrd_ = 0.3;

  // State dimension
  n_x_ = 5;

  // Augmented state dimension
  n_aug_ = 7;

  // Sigma point spreading parameter
  lambda_ = 3 - n_aug_;

  // initial state vector
  x_ = VectorXd(n_x_);

  // initial covariance matrix
  P_ = MatrixXd(n_x_, n_x_);

  // predicted sigma points matrix
  Xsig_pred_ = MatrixXd(n_x_, 2 * n_aug_ + 1);

  // Weights of sigma points
  weights_ = VectorXd(2 * n_aug_ + 1);

  // initialize
  x_ << 1, 1, 1, 1, 1;
  P_ << 1, 0, 0, 0, 0,
      0, 1, 0, 0, 0,
      0, 0, 1, 0, 0,
      0, 0, 0, 1, 0,
      0, 0, 0, 0, 1;

  for (int i = 1; i <= (2 * n_aug_ + 1); i++)
  {
    if (i == 1)
    {
      weights_(i - 1) = lambda_ / (lambda_ + n_aug_);
    }
    else
    {
      weights_(i - 1) = 1.0 / (2 * (lambda_ + n_aug_));
    }
  }

  // Initialize NIS
  NIS_LIDAR_ = 0.0;
  NIS_RADAR_ = 0.0;

  // Prepare matrices
  R_LIDAR_ = MatrixXd(2, 2);
  R_LIDAR_ << std_laspx_ * std_laspx_, 0,
      0, std_laspy_ * std_laspy_;

  R_RADAR_ = MatrixXd(3, 3);
  R_RADAR_ << std_radr_ * std_radr_, 0, 0,
      0, std_radphi_ * std_radphi_, 0,
      0, 0, std_radrd_ * std_radrd_;
}

UKF::~UKF() {}

/**
 * @param {MeasurementPackage} meas_package The latest measurement data of
 * either radar or laser.
 */
void UKF::ProcessMeasurement(MeasurementPackage meas_package)
{

  if (!is_initialized_)
  {
    if (meas_package.sensor_type_ == MeasurementPackage::RADAR)
    {
      float px_init = meas_package.raw_measurements_[0] * cos(meas_package.raw_measurements_[1]);
      float py_init = meas_package.raw_measurements_[0] * sin(meas_package.raw_measurements_[1]);

      x_ << px_init, py_init, 0, 0, 0;
    }

    if (meas_package.sensor_type_ == MeasurementPackage::LASER)
    {
      x_ << meas_package.raw_measurements_[0], meas_package.raw_measurements_[1], 0, 0, 0;
    }
    time_us_ = meas_package.timestamp_;
    is_initialized_ = true;
  }
  else
  {

    double delta_t = (meas_package.timestamp_ - time_us_) / 1000000.0;
    time_us_ = meas_package.timestamp_;
    Prediction(delta_t);

    if (use_radar_ && meas_package.sensor_type_ == MeasurementPackage::RADAR)
    {
      UpdateRadar(meas_package);
    }

    if (use_laser_ && meas_package.sensor_type_ == MeasurementPackage::LASER)
    {
      UpdateLidar(meas_package);
    }
  }
};

/**
 * Predicts sigma points, the state, and the state covariance matrix.
 * @param {double} delta_t the change in time (in seconds) between the last
 * measurement and this one.
 */
void UKF::Prediction(double delta_t)
{
  // ********************
  // Create Augmented Sigma Points
  // ********************

  // Setup
  VectorXd x_aug = VectorXd(7);
  MatrixXd P_aug = MatrixXd(7, 7);
  MatrixXd Xsig_aug = MatrixXd(n_aug_, 2 * n_aug_ + 1);

  //create augmented mean state
  x_aug.head(5) = x_;
  x_aug(5) = 0;
  x_aug(6) = 0;

  //create augmented covariance matrix
  P_aug.fill(0.0);
  P_aug.topLeftCorner(5, 5) = P_;
  P_aug(5, 5) = std_a_ * std_a_;
  P_aug(6, 6) = std_yawdd_ * std_yawdd_;

  //create square root matrix
  MatrixXd L = P_aug.llt().matrixL();

  //create augmented sigma points
  double sqrt_aug = sqrt(lambda_ + n_aug_);
  Xsig_aug.col(0) = x_aug;
  for (int i = 0; i < n_aug_; i++)
  {
    Xsig_aug.col(i + 1) = x_aug + sqrt_aug * L.col(i);
    Xsig_aug.col(i + 1 + n_aug_) = x_aug - sqrt_aug * L.col(i);
  }

  // ********************
  // Predict Sigma Points
  // ********************

  //predict sigma points
  for (int i = 0; i < 2 * n_aug_ + 1; i++)
  {
    //extract values for better readability
    const double p_x = Xsig_aug(0, i);
    const double p_y = Xsig_aug(1, i);
    const double v = Xsig_aug(2, i);
    const double yaw = Xsig_aug(3, i);
    const double yawd = Xsig_aug(4, i);
    const double nu_a = Xsig_aug(5, i);
    const double nu_yawdd = Xsig_aug(6, i);

    //predicted state values
    double px_p, py_p;

    //avoid division by zero
    if (fabs(yawd) > 0.001)
    {
      px_p = p_x + v / yawd * (sin(yaw + yawd * delta_t) - sin(yaw));
      py_p = p_y + v / yawd * (cos(yaw) - cos(yaw + yawd * delta_t));
    }
    else
    {
      px_p = p_x + v * delta_t * cos(yaw);
      py_p = p_y + v * delta_t * sin(yaw);
    }

    double v_p = v;
    double yaw_p = yaw + yawd * delta_t;
    double yawd_p = yawd;

    //add noise
    px_p = px_p + 0.5 * nu_a * delta_t * delta_t * cos(yaw);
    py_p = py_p + 0.5 * nu_a * delta_t * delta_t * sin(yaw);
    v_p = v_p + nu_a * delta_t;

    yaw_p = yaw_p + 0.5 * nu_yawdd * delta_t * delta_t;
    yawd_p = yawd_p + nu_yawdd * delta_t;

    //write predicted sigma point into right column
    Xsig_pred_(0, i) = px_p;
    Xsig_pred_(1, i) = py_p;
    Xsig_pred_(2, i) = v_p;
    Xsig_pred_(3, i) = yaw_p;
    Xsig_pred_(4, i) = yawd_p;

    // ********************
    // Predict Mean and Covariance
    // ********************

    //predicted state mean
    x_.fill(0.0);
    for (int i = 0; i < 2 * n_aug_ + 1; i++)
    { //iterate over sigma points
      x_ = x_ + weights_(i) * Xsig_pred_.col(i);
    }

    //predicted state covariance matrix
    MatrixXd P(n_x_, n_x_);
    P.fill(0.0);
    for (int i = 0; i < 2 * n_aug_ + 1; i++)
    { //iterate over sigma points

      // state difference
      VectorXd x_diff = Xsig_pred_.col(i) - x_;
      //angle normalization
      while (x_diff(3) > M_PI)
        x_diff(3) -= 2. * M_PI;
      while (x_diff(3) < -M_PI)
        x_diff(3) += 2. * M_PI;

      P += (x_diff * x_diff.transpose()) * weights_(i);
    }
    P_ = P;
  }
};

/**
 * Updates the state and the state covariance matrix using a laser measurement.
 * @param {MeasurementPackage} meas_package
 */
void UKF::UpdateLidar(MeasurementPackage meas_package)
{
  // ********************
  // Transform
  // ********************

  VectorXd z(2);
  z(0) = meas_package.raw_measurements_(0);
  z(1) = meas_package.raw_measurements_(1);

  MatrixXd Zsig(2, 2 * n_aug_ + 1);

  for (int i = 0; i < (2 * n_aug_ + 1); i++)
  {
    Zsig(0, i) = Xsig_pred_(0, i);
    Zsig(1, i) = Xsig_pred_(1, i);
  }

  VectorXd z_pred(2);
  z_pred.fill(0);
  for (int i = 0; i < (2 * n_aug_ + 1); i++)
  {
    z_pred = z_pred + Zsig.col(i) * weights_(i);
  }

  // ********************
  // Update measurement
  // ********************
  MatrixXd S(2, 2);
  S.fill(0);
  for (int i = 0; i < (2 * n_aug_ + 1); i++)
  {
    VectorXd diff = Zsig.col(i) - z_pred;

    S = S + weights_(i) * diff * diff.transpose();
  }

  S = S + R_LIDAR_;

  // Update State
  int n_z = z_pred.size();
  MatrixXd T(n_x_, n_z);
  T.fill(0);

  for (int i = 0; i < (2 * n_aug_ + 1); i++)
  {
    VectorXd x_diff = Xsig_pred_.col(i) - x_;
    VectorXd z_diff = Zsig.col(i) - z_pred;
    T = T + weights_(i) * x_diff * z_diff.transpose();
  }

  MatrixXd K = T * S.inverse();
  VectorXd z_diff = z - z_pred;
  x_ = x_ + K * z_diff;
  P_ = P_ - K * S * K.transpose();

  // ********************
  // NIS for Lidar
  // ********************
  NIS_LIDAR_ = z_diff.transpose() * S.inverse() * z_diff;
  cout << "NIS_LIDAR_" << NIS_LIDAR_ << endl;
};

/**
 * Updates the state and the state covariance matrix using a radar measurement.
 * @param {MeasurementPackage} meas_package
 */
void UKF::UpdateRadar(MeasurementPackage meas_package)
{
  // ********************
  // Transform
  // ********************
  VectorXd z(3);
  z(0) = meas_package.raw_measurements_(0);
  z(1) = meas_package.raw_measurements_(1);
  z(2) = meas_package.raw_measurements_(2);

  MatrixXd Zsig(3, 2 * n_aug_ + 1);
  for (int i = 0; i < (2 * n_aug_ + 1); i++)
  {
    double p_x = Xsig_pred_(0, i);
    double p_y = Xsig_pred_(1, i);
    double v = Xsig_pred_(2, i);
    double yaw = Xsig_pred_(3, i);
    double yawd = Xsig_pred_(4, i);

    double rho = sqrt(p_x * p_x + p_y * p_y);
    double phi = atan2(p_y, p_x);
    double rho_dot = ((p_x * cos(yaw) * v) + (p_y * sin(yaw) * v)) / rho;

    Zsig(0, i) = rho;
    Zsig(1, i) = phi;
    Zsig(2, i) = rho_dot;
  }

  VectorXd z_pred(3);
  z_pred.fill(0);
  for (int i = 0; i < (2 * n_aug_ + 1); i++)
  {
    z_pred = z_pred + Zsig.col(i) * weights_(i);
  }

  // ********************
  // Update measurement
  // ********************
  MatrixXd S(3, 3);
  S.fill(0);
  for (int i = 0; i < (2 * n_aug_ + 1); i++)
  {
    VectorXd diff = Zsig.col(i) - z_pred;

    while (diff(1) > M_PI)
      diff(1) -= 2. * M_PI;
    while (diff(1) < -M_PI)
      diff(1) += 2. * M_PI;

    S = S + weights_(i) * diff * diff.transpose();
  }

  S = S + R_RADAR_;

  int n_z = z_pred.size();
  MatrixXd T(n_x_, n_z);
  T.fill(0);

  for (int i = 0; i < (2 * n_aug_ + 1); i++)
  {
    VectorXd x_diff = Xsig_pred_.col(i) - x_;
    // angle normalization
    while (x_diff(3) > M_PI)
      x_diff(3) -= 2. * M_PI;
    while (x_diff(3) < -M_PI)
      x_diff(3) += 2. * M_PI;

    VectorXd z_diff = Zsig.col(i) - z_pred;
    // angle normalization
    while (z_diff(1) > M_PI)
      z_diff(1) -= 2. * M_PI;
    while (z_diff(1) < -M_PI)
      z_diff(1) += 2. * M_PI;

    T = T + weights_(i) * x_diff * z_diff.transpose();
  }

  MatrixXd K = T * S.inverse();

  VectorXd z_diff = z - z_pred;
  // angle normalization
  while (z_diff(1) > M_PI)
    z_diff(1) -= 2. * M_PI;
  while (z_diff(1) < -M_PI)
    z_diff(1) += 2. * M_PI;

  x_ = x_ + K * z_diff;

  P_ = P_ - K * S * K.transpose();

  // ********************
  // NIS for Radar
  // ********************
  NIS_RADAR_ = z_diff.transpose() * S.inverse() * z_diff;
  cout << "NIS_RADAR_" << NIS_RADAR_ << endl;
};
