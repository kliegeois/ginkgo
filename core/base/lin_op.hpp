#ifndef GINKGO_CORE_BASE_LIN_OP_HPP_
#define GINKGO_CORE_BASE_LIN_OP_HPP_


#include "core/base/executor.hpp"
#include "core/base/types.hpp"


#include <memory>


namespace gko {


/**
 * The linear operator (LinOp) is a base class for all linear algebra objects
 * in MAGMA-sparse. The main benefit of having a single base class for the
 * entire collection of linear algebra objects (as opposed to having separate
 * hierarchies for matrices, solvers and preconditioners) is the generality
 * it provides.
 *
 * First, since all subclasses provide a common interface, the library users are
 * exposed to a smaller set of routines. For example, a
 * matrix-vector product, a preconditioner application, or even a system solve
 * are just different terms given to the operation of applying a certain linear
 * operator to a vector. As such, MAGMA-sparse uses the same routine name,
 * LinOp::apply() for each of these operations, where the actual
 * operation performed depends on the type of linear operator involved in
 * the operation.
 *
 * Second, a common interface often allows for writing more generic code. If a
 * user's routine requires only operations provided by the LinOp interface,
 * the same code can be used for any kind of linear operators, independent of
 * whether these are matrices, solvers or preconditioners. This feature is also
 * extensively used in MAGMA-sparse itself. For example, a preconditioner used
 * inside a Krylov solver is a LinOp. This allows the user to supply a wide
 * variety of preconditioners: either the ones which were designed to be used
 * in this scenario (like ILU or block-Jacobi), a user-supplied matrix which is
 * known to be a good preconditioner for the specific problem,
 * or even another solver (e.g., if constructing a flexible GMRES solver).
 *
 * A key observation for providing a unified interface for matrices, solvers,
 * and preconditioners is that the most common operation performed on all of
 * them can be expressed as an application of a liner operator to a vector:
 *
 * +   the sparse matrix-vector product with a matrix \f$A\f$ is a linear
 *     operator application \f$y = Ax\f$;
 * +   the application of a preconditioner is a linear operator application
 *     \f$y = M^{-1}x\f$, where \f$M\f$ is an approximation of the original
 *     system matrix \f$A\f$ (thus a preconditioner represents an "approximate
 *     inverse" operator \f$M^{-1}\f$).
 * +   the system solve \f$Ax = b\f$ can be viewed as linear operator
 *     application
 *     \f$x = A^{-1}b\f$ (it goes without saying that the implementation of
 *     linear system solves does not follow this conceptual idea), so a linear
 *     system solver can be viewed as a representation of the operator
 *     \f$A^{-1}\f$.
 *
 * An accompanying routine to the linear operator application is its
 * generation from another linear operator \f$A\f$: a solver operator
 * \f$A^{-1}\f$ is generated by applying the inverse function, a preconditioner
 * operator \f$M^{-1}\f$ by applying the approximate inverse function, and
 * a matrix by just applying the identity operator \f$id: A \rightarrow A\f$.
 * Thus, every LinOp subclass has a meaningful implementation of the
 * LinOp::generate() routine, which performs the above mentioned operation.
 *
 * Formally speaking, a MAGMA-sparse LinOp subclass does not represent a linear
 * operator, but in fact a (non-linear) operator \f$op\f$ on the space of all
 * linear operators (on a certain vector space). In case of matrices
 * \f$op\f$ is the identity operator, in case of solvers the inverse operator,
 * and in case of preconditioners a type of approximate inverse operator
 * (with different preconditioner classes representing different operators).
 * The LinOp::generate() routine can then be viewed as an application of this
 * operator to a linear operator \f$A\f$, yielding the result \f$op(A)\f$ and
 * the LinOp::apply() routine as an application \f$y = op(A)x\f$ of the
 * resulting linear operator to a vector \f$x\f$.
 *
 * Finally, direct manipulation of LinOp objects is rarely required in
 * simple scenarios. As an ilustrative example, one could construct a
 * fixed-point iteration routine \f$x_{k+1} = Lx_k + b\f$ as follows:
 *
 * ```cpp
 * std::unique_ptr<msparse::DenseMatrix<double>> calculate_fixed_point(
 *         int iters,
 *         const msparse::LinOp<double> *L,
 *         const msparse::DenseMatrix<double> *x0
 *         const msparse::DenseMatrix<double> *b)
 * {
 *     auto x = msparse::clone(x0.get());
 *     auto tmp = msparse::clone(x0.get());
 *     for (int i = 0; i < iters; ++i) {
 *         L->apply(tmp.get(), x.get());
 *         x->axpy(1.0, b.get());
 *         tmp->copy_from(x.get());
 *     }
 *     return x;
 * }
 * ```
 * Here, as \f$L\f$ is a matrix, LinOp::apply() refers to the matrix vector
 * product, and `L->apply(a, b)` computes \f$b = L \cdot a\f$.
 * `x->axpy(1.0, b.get())` is the `axpy` vector update \f$x:=x+b\f$.
 *
 * The interesting part of this example is the apply() routine at line 4 of the
 * function body. Since this routine is part of the LinOp base class, the
 * fixed-point iteration routine can calculate a fixed point not only for
 * matrices, but for any type of linear operator.
 *
 * @tparam E  the precision the data is stored in
 *
 * @internal
 * The LinOp class represents a linear operator over an n-dimensional vector
 * space over the field E and provides functionalities
 * common to all suported linear operators: Matrix, Precond, Solver.
 */
class LinOp {
public:
    /**
     * Create a copy of another LinOp.
     *
     * @param other  the LinOp to copy
     *
     * @throw NotSupported  other is of incompatible type
     */
    virtual void copy_from(const LinOp *other) = 0;

    /**
     * Move the data from another LinOp.
     *
     * @param other  the LinOp from which the data will be moved
     *
     * @throw NotSupported  other is of incompatible type
     */
    virtual void copy_from(std::unique_ptr<LinOp> other) = 0;

    /**
     * Apply a linear operator to a vector (or a sequence of vectors).
     *
     * Performs the operation x = op(b), where op is this linear operator.
     *
     * @param b  the input vector on which the operator is applied
     * @param x  the output vector where the result is stored
     *
     * @throw DimensionMismatch  the LinOp and the vectors are of incompatible
     *                           sizes
     */
    virtual void apply(const LinOp *b, LinOp *x) const = 0;

    /**
     * Perform the operation x = alpha * op(b) + beta * x.
     *
     * @param alpha  scaling of the result of op(b)
     * @param b  vector on which the operator is applied
     * @param beta  scaling of the input x
     * @param x  output vector
     *
     * @throw DimensionMismatch  the LinOp and the vectors are of incompatible
     *                           sizes
     */
    virtual void apply(full_precision alpha, const LinOp *b,
                       full_precision beta, LinOp *x) const = 0;

    /**
     * @internal
     * Create a copy of the LinOp.
     */
    std::unique_ptr<LinOp> clone() const
    {
        auto new_op = this->clone_type();
        new_op->copy_from(this);
        return new_op;
    }

    /**
     * Create a new 0x0 LinOp of the same type.
     *
     * @return  a LinOp object of the same type as this
     */
    virtual std::unique_ptr<LinOp> clone_type() const = 0;

    /**
     * Transform the object into an empty LinOp.
     */
    virtual void clear() = 0;

    /**
     * Get the Executor of this object.
     */
    std::shared_ptr<const Executor> get_executor() const noexcept
    {
        return exec_;
    }

    /**
     * Get the dimension of the codomain of this LinOp.
     *
     * In other words, the number of rows of the coefficient matrix.
     *
     * @return the dimension of the codomain
     */
    size_type get_num_rows() const noexcept { return num_rows_; }

    /**
     * Get the dimension of the domain of this LinOp.
     *
     * In other words, the number of columns of the coefficient matrix.
     *
     * @return the dimension of the codomain
     */
    size_type get_num_cols() const noexcept { return num_cols_; }

    /**
     * Get an upper bound on the number of nonzero values in the coefficient
     * matrix of the operator.
     *
     * This routine will get the number of nonzeros as seen by the library,
     * and as used in computations, while the real number of nonzeros might
     * be significantly lower than this value.
     *
     * For example, for a DenseMatrix `A` it will always hold
     * ```cpp
     * A->get_num_nonzeros() == A->get_num_rows() * A->get_num_cols()
     * ```
     *
     * @return the number of nonzeros as seen by the library
     */
    size_type get_num_nonzeros() const noexcept { return num_nonzeros_; }

protected:
    /**
     * Create a new LinOp object on the specified Executor.
     */
    LinOp(std::shared_ptr<const Executor> exec, size_type num_rows,
          size_type num_cols, size_type num_nonzeros)
        : exec_(exec),
          num_rows_(num_rows),
          num_cols_(num_cols),
          num_nonzeros_(num_nonzeros)
    {}

    void set_dimensions(size_type num_rows, size_type num_cols,
                        size_type num_nonzeros) noexcept
    {
        num_rows_ = num_rows;
        num_cols_ = num_cols;
        num_nonzeros_ = num_nonzeros;
    }

private:
    std::shared_ptr<const Executor> exec_;
    size_type num_rows_;
    size_type num_cols_;
    size_type num_nonzeros_;
};


}  // namespace gko


#endif  // GINKGO_CORE_BASE_LIN_OP_HPP_
